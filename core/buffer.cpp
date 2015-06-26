#include <climits>
#include <algorithm>

#include "buffer.hpp"
#include "except/exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

static int const BUFFER_SIZE = 16 * 1024;
static int const WRITEV_MAX_SIZE = 2 * 1024 * 1024;

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
    for (char ch: s) {
        b._buffer.push_back(byte(ch));
    }
    return std::move(b);
}

int Buffer::read(int fd)
{
    byte local[BUFFER_SIZE];
    int n = 0, nread;
    while ((nread = cio::read(fd, local, BUFFER_SIZE)) > 0) {
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
        int nwrite = cio::write(fd, _buffer.data() + n, _buffer.size() - n);
        if (nwrite == -1) {
            on_error("buffer write");
            continue;
        }
        n += nwrite;
    }
    return n;
}

void Buffer::truncate_from_begin(iterator i)
{
    this->_buffer.erase(_buffer.begin(), i);
}

void Buffer::buffer_ready(std::vector<cio::iovec>& iov)
{
    if (!_buffer.empty()) {
        cio::iovec v = {_buffer.data(), size_t(_buffer.size())};
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

static int write_single(int fd, byte const* buf, int buf_len, int* offset)
{
    while (*offset < buf_len) {
        ssize_t nwritten = cio::write(fd, buf + *offset, buf_len - *offset);
        if (nwritten == -1) {
            on_error("buffer write");
            return 0;
        }
        LOG(DEBUG) << "Write to " << fd << " : " << nwritten << " bytes written";
        *offset += nwritten;
    }
    return 1;
}

static int write_vec(int fd, int iovcnt, cio::iovec* iov, ssize_t total, int* first_offset)
{
    if (1 == iovcnt) {
        return write_single(fd, reinterpret_cast<byte*>(iov->iov_base),
                            iov->iov_len, first_offset);
    }

    LOG(DEBUG) << "*writev to " << fd << " iovcnt=" << iovcnt << " total bytes=" << total;
    iov[0].iov_base = reinterpret_cast<byte*>(iov->iov_base) + *first_offset;
    iov->iov_len -= *first_offset;
    int written_iov = 0;
    ssize_t nwritten;
    while (total != (nwritten = cio::writev(fd, iov + written_iov, iovcnt - written_iov))) {
        if (nwritten == 0) {
            return written_iov;
        }
        if (nwritten == -1) {
            on_error("buffer writev");
            return written_iov;
        }
        LOG(DEBUG) << "*writev partial: " << nwritten << " / " << total;
        total -= nwritten;
        while (iov[written_iov].iov_len <= size_t(nwritten)) {
            nwritten -= iov[written_iov].iov_len;
            ++written_iov;
            *first_offset = 0;
        }
        iov[written_iov].iov_base = reinterpret_cast<byte*>(iov[written_iov].iov_base) + nwritten;
        iov[written_iov].iov_len -= nwritten;
        *first_offset += nwritten;
    }
    return iovcnt;
}

static std::pair<int, int> next_group_to_write(
        std::deque<util::sref<Buffer>> const& buf_arr, int first_offset, cio::iovec* vec)
{
    vec[0].iov_base = buf_arr[0]->data();
    vec[0].iov_len = buf_arr[0]->size();
    Buffer::size_type bulk_write_size = buf_arr[0]->size() - first_offset;
    std::deque<util::sref<Buffer>>::size_type i = 1;
    for (; i < buf_arr.size()
            && i < IOV_MAX
            && bulk_write_size + buf_arr[i]->size() <= WRITEV_MAX_SIZE;
         ++i)
    {
        vec[i].iov_base = buf_arr[i]->data();
        vec[i].iov_len = buf_arr[i]->size();
        bulk_write_size += vec[i].iov_len;
    }
    return std::pair<int, int>(i, bulk_write_size);
}

bool BufferSet::writev(int fd)
{
    cio::iovec vec[IOV_MAX];
    while (!this->_buf_arr.empty()) {
        auto x = ::next_group_to_write(this->_buf_arr, this->_1st_buf_offset, vec);
        int iovcnt = x.first;
        int bulk_write_size = x.second;
        int iov_written = ::write_vec(fd, iovcnt, vec, bulk_write_size,
                                      &this->_1st_buf_offset);
        this->_buf_arr.erase(this->_buf_arr.begin(), this->_buf_arr.begin() + iov_written);
        if (iov_written < iovcnt) {
            return false;
        }
        this->_1st_buf_offset = 0;
    }
    return true;
}
