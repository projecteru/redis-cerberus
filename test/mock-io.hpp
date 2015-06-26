#ifndef __CERBERUS_TEST_MOCK_IO_HPP__
#define __CERBERUS_TEST_MOCK_IO_HPP__

#include <deque>
#include <string>
#include <gtest/gtest.h>

#include "utils/pointer.h"
#include "syscalls/cio.h"
#include "syscalls/fctl.h"

class CIOImplement {
    static util::sptr<CIOImplement> _p;
public:
    virtual ~CIOImplement() {}

    static void set_impl(util::sptr<CIOImplement> p);
    static util::sref<CIOImplement> get_impl();

    virtual ssize_t read(int fd, void* buf, size_t count);
    virtual ssize_t write(int fd, void const* buf, size_t count);
    virtual ssize_t writev(int fd, cio::iovec const* iov, int iovcnt);
    virtual int close(int fd);

    virtual int new_stream_socket() { return -1; }
    virtual int set_tcpnodelay(int) { return 0; }
    virtual void set_nonblocking(int) {}
    virtual void connect_fd(std::string const&, int, int) {}
    virtual void bind_to(int, int) {}
};

struct BufferIO
    : CIOImplement
{
    std::deque<std::string> read_buffer;
    std::deque<std::string> write_buffer;
    std::deque<int> writing_sizes;

    void clear();
    ssize_t read(int, void* b, size_t count);
    ssize_t write(int, void const* buf, size_t count);
    int close(int);
};

struct BufferTestBase
    : testing::Test
{
    static util::sref<BufferIO> io_obj;

    void SetUp();
    void TearDown();
};

#endif /* __CERBERUS_TEST_MOCK_IO_HPP__ */
