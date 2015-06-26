#ifndef __CERBERUS_SYSTEM_FILE_CONTROL_H__
#define __CERBERUS_SYSTEM_FILE_CONTROL_H__

#include <string>

#ifndef _USE_CANDIDATE_FCTL_LIB

#include "except/exceptions.hpp"

#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace fctl {

    inline int set_tcpnodelay(int sockfd)
    {
        int nodelay = 1;
        socklen_t len = sizeof nodelay;
        return ::setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, len);
    }

    inline void set_nonblocking(int sockfd)
    {
        int opts;
        opts = ::fcntl(sockfd, F_GETFL);
        if (opts < 0) {
            throw cerb::SystemError("fcntl:getfl", errno);
        }
        opts = (opts | O_NONBLOCK);
        if (::fcntl(sockfd, F_SETFL, opts) < 0) {
            throw cerb::SystemError("fcntl:setfl", errno);
        }
    }

    inline int new_stream_socket()
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            throw cerb::SocketCreateError("Server create", errno);
        }
        return fd;
    }

    inline void connect_fd(std::string const& host, int port, int fd)
    {
        set_tcpnodelay(fd);
        struct sockaddr_in serv_addr;
        ::bzero(&serv_addr, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;
        if (::inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) != 1) {
            throw cerb::UnknownHost(host);
        }
        serv_addr.sin_port = htons(port);
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&serv_addr),
                      sizeof serv_addr) < 0)
        {
            if (errno == EINPROGRESS) {
                return;
            }
            throw cerb::ConnectionRefused(host, port, errno);
        }
    }

    inline void bind_to(int fd, int port)
    {
        int option = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
                         &option, sizeof option) < 0)
        {
            throw cerb::SystemError("set reuseport", errno);
        }
        struct sockaddr_in local;
        ::bzero(&local, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(port);
        if (::bind(fd, reinterpret_cast<struct sockaddr*>(&local), sizeof local) < 0) {
            throw cerb::SystemError("bind", errno);
        }
        ::listen(fd, 20);
    }

}

#else /* _USE_CANDIDATE_FCTL_LIB */

namespace fctl {

    int new_stream_socket();
    int set_tcpnodelay(int sockfd);
    void set_nonblocking(int sockfd);
    void connect_fd(std::string const& host, int port, int fd);
    void bind_to(int fd, int port);

}

#endif /* _USE_CANDIDATE_FCTL_LIB */

#endif /* __CERBERUS_SYSTEM_FILE_CONTROL_H__ */
