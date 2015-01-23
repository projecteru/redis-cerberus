#include <unistd.h>
#include <sys/uio.h>
#include <algorithm>

#include "buffer.hpp"
#include "exceptions.hpp"
#include "globals.hpp"
#include "fdutil.hpp"
#include "utils/logging.hpp"

using namespace cerb;
using util::MemPage;
using util::SharedMemPage;
using util::MemoryPages;

static void page_del(MemPage* p)
{
    cerb_global::page_pool.destroy(p);
}

static std::shared_ptr<MemPage> alloc_page()
{
    return std::shared_ptr<MemPage>(cerb_global::page_pool.create(), page_del);
}

static std::shared_ptr<MemPage> alloc_page(std::string const& s)
{
    return std::shared_ptr<MemPage>(cerb_global::page_pool.create(s), page_del);
}

static SharedMemPage make_page(std::string const& s)
{
    return SharedMemPage(alloc_page(s), s.size());
}

Buffer Buffer::from_string(std::string const& s)
{
    Buffer b;
    b._buffer.append_page(make_page(s));
    return std::move(b);
}

int Buffer::read(int fd)
{
    int n = 0, nread;
    for (auto page = alloc_page();
         0 < (nread = ::read(fd, page->page, MemPage::PAGE_SIZE));
         page = alloc_page())
    {
        n += nread;
        this->_buffer.append_page(SharedMemPage(page, nread));
    }
    if (nread == -1) {
        if (errno != EAGAIN) {
            throw IOError("buffer read", errno);
        }
        if (n == 0) {
            return -1;
        }
    }
    return n;
}

void Buffer::write(int fd)
{
    std::vector<struct iovec> iov;
    buffer_ready(iov);
    cerb::writev(fd, this->size(), iov);
}

void Buffer::truncate_from_begin(const_iterator i)
{
    this->_buffer.erase_from_begin(i);
}

void Buffer::buffer_ready(std::vector<struct iovec>& iov)
{
    for (auto page_it = _buffer.pages_begin(); page_it != _buffer.pages_end();
         ++page_it)
    {
        struct iovec v = {page_it->page(), size_t(page_it->size)};
        LOG(DEBUG) << "Push iov " << reinterpret_cast<void*>(page_it->page()) << ' ' << page_it->size;
        iov.push_back(v);
    }
}

void Buffer::append_from(const_iterator first, const_iterator last)
{
    _buffer.append_range(first, last);
}

std::string Buffer::to_string() const
{
    std::string s;
    for (auto page_it = _buffer.pages_begin(); page_it != _buffer.pages_end();
         ++page_it)
    {
        s.insert(s.end(), page_it->page(), page_it->page() + page_it->size);
    }
    return std::move(s);
}

bool Buffer::same_as_string(std::string const& s) const
{
    if (size() != s.size()) {
        return false;
    }
    return to_string() == s;
}
