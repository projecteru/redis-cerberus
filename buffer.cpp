#include <unistd.h>
#include <sys/uio.h>

#include "buffer.hpp"
#include "exceptions.hpp"

using namespace cerb;

int const BUFFER_SIZE = 16 * 1024;

int Buffer::read(int fd)
{
    byte local[BUFFER_SIZE];
    int n = 0, nread;
    while ((nread = ::read(fd, local, BUFFER_SIZE)) > 0) {
        n += nread;
        this->_buffer.insert(this->_buffer.end(), local, local + nread);
    }
    if (nread == -1 && errno != EAGAIN) {
        throw IOError("buffer read", errno);
    }
    return n;
}

int Buffer::write(int fd)
{
    int n = _buffer.size(), nwrite;
    while (n > 0) {
        nwrite = ::write(fd, _buffer.data() + _buffer.size() - n, n);
        if (nwrite == -1 && errno != EAGAIN) {
            throw IOError("buffer write", errno);
        }
        n -= nwrite;
    }
    return n;
}

void Buffer::truncate_from_begin(iterator i)
{
    this->_buffer.erase(_buffer.begin(), i);
}

void Buffer::buffer_ready(std::vector<struct iovec>& iov)
{
    struct iovec v = {_buffer.data(), size_t(_buffer.size())};
    iov.push_back(v);
}

void Buffer::copy_from(const_iterator first, const_iterator last)
{
    _buffer.clear();
    _buffer.insert(_buffer.end(), first, last);
}

std::string Buffer::to_string() const
{
    return std::string((char const*)(_buffer.data()));
}
