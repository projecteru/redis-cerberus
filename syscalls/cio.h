#ifndef __CERBERUS_SYSTEM_CIO_H__
#define __CERBERUS_SYSTEM_CIO_H__

#ifndef _USE_CANDIDATE_IO_LIB

#include <unistd.h>
#include <sys/uio.h>
#include <netdb.h>

namespace cio {

    typedef struct ::iovec iovec;

    inline ssize_t read(int fd, void* buf, size_t count)
    {
        return ::read(fd, buf, count);
    }

    inline ssize_t write(int fd, void const* buf, size_t count)
    {
        return ::write(fd, buf, count);
    }

    inline ssize_t writev(int fd, iovec const* iov, int iovcnt)
    {
        return ::writev(fd, iov, iovcnt);
    }

    inline int close(int fd)
    {
        return ::close(fd);
    }

    inline int accept(int accfd)
    {
        struct sockaddr_in remote;
        socklen_t addrlen = sizeof remote;
        return ::accept(accfd, reinterpret_cast<struct sockaddr*>(&remote), &addrlen);
    }

}

#else /* _USE_CANDIDATE_IO_LIB */

#include <sys/types.h>

namespace cio {

    struct iovec {
        void* iov_base;
        size_t iov_len;
    };

    ssize_t read(int fd, void* buf, size_t count);
    ssize_t write(int fd, void const* buf, size_t count);
    ssize_t writev(int fd, iovec const* iov, int iovcnt);
    int close(int fd);
    int accept(int accfd);

}

#endif /* _USE_CANDIDATE_IO_LIB */

#endif /* __CERBERUS_SYSTEM_CIO_H__ */
