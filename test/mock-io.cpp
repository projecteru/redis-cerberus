#include <unistd.h>
#include <cerrno>

#include "mock-io.hpp"
#include "syscalls/cio.h"

void CIOImplement::set_impl(util::sptr<CIOImplement> p)
{
    _p = std::move(p);
}

util::sref<CIOImplement> CIOImplement::get_impl()
{
    return *_p;
}

util::sptr<CIOImplement> CIOImplement::_p(new CIOImplement);

ssize_t CIOImplement::read(int fd, void* buf, size_t count)
{
    return ::read(fd, buf, count);
}

ssize_t CIOImplement::write(int fd, void const* buf, size_t count)
{
    return ::write(fd, buf, count);
}

/* This is also an example for systems that does not support writev */

ssize_t CIOImplement::writev(int fd, cio::iovec const* iov, int iovcnt)
{
    ssize_t n = 0;
    for (int i = 0; i < iovcnt; ++i) {
        ssize_t s = this->write(fd, iov[i].iov_base, iov[i].iov_len);
        if (-1 == s) {
            return n ? n : -1;
        }
        if (s < ssize_t(iov[i].iov_len)) {
            return n + s;
        }
        n += s;
    }
    return n;
}

int CIOImplement::close(int fd)
{
    return ::close(fd);
}

ssize_t cio::read(int fd, void* buf, size_t count)
{
    return CIOImplement::get_impl()->read(fd, buf, count);
}

ssize_t cio::write(int fd, void const* buf, size_t count)
{
    return CIOImplement::get_impl()->write(fd, buf, count);
}

ssize_t cio::writev(int fd, cio::iovec const* iov, int iovcnt)
{
    return CIOImplement::get_impl()->writev(fd, iov, iovcnt);
}

int cio::close(int fd)
{
    return CIOImplement::get_impl()->close(fd);
}

int fctl::new_stream_socket()
{
    return CIOImplement::get_impl()->new_stream_socket();
}

int fctl::set_tcpnodelay(int fd)
{
    return CIOImplement::get_impl()->set_tcpnodelay(fd);
}

void fctl::set_nonblocking(int sockfd)
{
    return CIOImplement::get_impl()->set_nonblocking(sockfd);
}

void fctl::connect_fd(std::string const& host, int port, int fd)
{
    return CIOImplement::get_impl()->connect_fd(host, port, fd);
}

void fctl::bind_to(int fd, int port)
{
    return CIOImplement::get_impl()->bind_to(fd, port);
}

void BufferIO::clear()
{
    this->read_buffer.clear();
    this->write_buffer.clear();
    this->writing_sizes.clear();
}

ssize_t BufferIO::read(int, void* b, size_t count)
{
    if (this->read_buffer.empty()) {
        errno = EAGAIN;
        return -1;
    }
    char* buf = static_cast<char*>(b);
    if (count < this->read_buffer[0].size()) {
        std::copy(this->read_buffer[0].begin(), this->read_buffer[0].begin() + count, buf);
        this->read_buffer[0].erase(this->read_buffer[0].begin(), this->read_buffer[0].begin() + count);
        return count;
    }
    size_t sz = this->read_buffer[0].size();
    std::copy(this->read_buffer[0].begin(), this->read_buffer[0].end(), buf);
    this->read_buffer.pop_front();
    return sz;
}

ssize_t BufferIO::write(int, void const* buf, size_t count)
{
    if (this->writing_sizes.empty()) {
        this->write_buffer.push_back(std::string(static_cast<char const*>(buf), count));
        return count;
    }
    int sz = this->writing_sizes[0];
    if (-1 == sz) {
        this->writing_sizes.pop_front();
        errno = EAGAIN;
        return -1;
    }
    if (sz <= int(count)) {
        this->write_buffer.push_back(std::string(static_cast<char const*>(buf), sz));
        this->writing_sizes[0] = -1;
        return sz;
    }
    this->write_buffer.push_back(std::string(static_cast<char const*>(buf), count));
    this->writing_sizes[0] -= count;
    return count;
}

int BufferIO::close(int)
{
    EXPECT_TRUE(false);
    return 0;
}

util::sref<BufferIO> BufferTestBase::io_obj(nullptr);

void BufferTestBase::SetUp()
{
    util::sptr<BufferIO> buffer_io(new BufferIO);
    BufferTestBase::io_obj = *buffer_io;
    CIOImplement::set_impl(std::move(buffer_io));
}

void BufferTestBase::TearDown()
{
    BufferTestBase::io_obj.reset();
    CIOImplement::set_impl(util::mkptr(new CIOImplement));
}
