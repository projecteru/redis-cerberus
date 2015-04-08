#include <climits>
#include <unistd.h>
#include <sys/uio.h>
#include <algorithm>

#include "buffer.hpp"
#include "exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

static int const BUFFER_SIZE = 16 * 1024;
static int const WRITEV_CHUNK_SIZE = 2 * 1024 * 1024;

static void on_error(std::string const& message)
{
    if (errno == EAGAIN) {
        return;
    }
    if (errno == ETIMEDOUT || errno == ECONNABORTED || errno == ECONNREFUSED
            || errno == ECONNRESET || errno == EHOSTUNREACH || errno == EIO)
    {
        throw IOError(message, errno);
    }
    throw SystemError(message, errno);
}

Buffer Buffer::from_string(std::string const& s)
{
    Buffer b;
    std::for_each(s.begin(), s.end(),
                  [&](char ch)
                  {
                      b._buffer.push_back(byte(ch));
                  });
    return std::move(b);
}

int Buffer::read(int fd)
{
    byte local[BUFFER_SIZE];
    int n = 0, nread;
    while ((nread = ::read(fd, local, BUFFER_SIZE)) > 0) {
        n += nread;
        this->_buffer.insert(this->_buffer.end(), local, local + nread);
    }
    if (nread == -1) {
        on_error("buffer read");
    }
    return n;
}

int Buffer::write(int fd) const
{
    size_type n = 0;
    while (n < _buffer.size()) {
        int nwrite = ::write(fd, _buffer.data() + n, _buffer.size() - n);
        if (nwrite == -1) {
            on_error("buffer write");
            continue;
        }
        LOG(DEBUG) << "Write to " << fd << " : " << nwrite << " bytes written";
        n += nwrite;
    }
    LOG(DEBUG) << "Total written " << fd << " : " << n << " bytes";
    return n;
}

void Buffer::truncate_from_begin(iterator i)
{
    this->_buffer.erase(_buffer.begin(), i);
}

void Buffer::buffer_ready(std::vector<struct iovec>& iov)
{
    if (!_buffer.empty()) {
        struct iovec v = {_buffer.data(), size_t(_buffer.size())};
        LOG(DEBUG) << "Push iov " << reinterpret_cast<void*>(_buffer.data()) << ' ' << _buffer.size();
        iov.push_back(v);
    }
}

void Buffer::copy_from(const_iterator first, const_iterator last)
{
    _buffer.clear();
    append_from(first, last);
}

void Buffer::append_from(const_iterator first, const_iterator last)
{
    _buffer.insert(_buffer.end(), first, last);
}

std::string Buffer::to_string() const
{
    return std::string(reinterpret_cast<char const*>(_buffer.data()), size());
}

bool Buffer::same_as_string(std::string const& s) const
{
    if (size() != s.size()) {
        return false;
    }
    std::string::size_type i = 0;
    return cend() == std::find_if(cbegin(), cend(), [&](byte b)
                                                    {
                                                        return b != s[i++];
                                                    });
}

static void write_vec(int fd, int iovcnt, struct iovec* iov, ssize_t total)
{
    LOG(DEBUG) << "*writev to " << fd << " iovcnt=" << iovcnt << " total bytes=" << total;
    ssize_t written;
    while (total != (written = ::writev(fd, iov, iovcnt))) {
        if (written == -1) {
            on_error("buffer writev");
            continue;
        }
        LOG(DEBUG) << "*writev partial written bytes=" << written << " need to write=" << total;
        total -= written;
        while (iov->iov_len <= size_t(written)) {
            written -= iov->iov_len;
            ++iov;
            --iovcnt;
        }
        iov->iov_base = reinterpret_cast<byte*>(iov->iov_base) + written;
        iov->iov_len -= written;
    }
}

void Buffer::writev(int fd, std::vector<util::sref<Buffer>> const& arr)
{
    struct iovec vec[IOV_MAX];
    int iov_index = 0;
    size_type bulk_write_size = 0;
    if (arr.size() == 1) {
        arr[0]->write(fd);
        return;
    }
    for (auto b: arr) {
        if (iov_index == IOV_MAX
            || bulk_write_size + b->size() > WRITEV_CHUNK_SIZE)
        {
            write_vec(fd, iov_index, vec, bulk_write_size);
            iov_index = 0;
            bulk_write_size = 0;
        }
        if (b->size() > WRITEV_CHUNK_SIZE) {
            b->write(fd);
            continue;
        }
        vec[iov_index].iov_base = b->_buffer.data();
        vec[iov_index].iov_len = b->size();
        ++iov_index;
        bulk_write_size += b->size();
    }
    if (iov_index == 1) {
        arr.back()->write(fd);
        return;
    }
    if (iov_index > 1) {
        write_vec(fd, iov_index, vec, bulk_write_size);
    }
}
