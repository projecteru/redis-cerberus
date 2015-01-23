#ifndef __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__
#define __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__

#include <string>
#include <vector>

struct iovec;

namespace cerb {

    class FDWrapper {
    public:
        int fd;

        FDWrapper(int fd)
            : fd(fd)
        {}

        FDWrapper(FDWrapper const&) = delete;

        ~FDWrapper();
    };

    int new_stream_socket();
    int set_tcpnodelay(int sockfd);
    void set_nonblocking(int sockfd);
    void connect_fd(std::string const& host, int port, int fd);
    void bind_to(int fd, int port);
    void writev(int fd, int total_bytes, std::vector<struct iovec> const& iov);

}

#endif /* __CERBERUS_FILE_DESCRIPTER_UTILITY_HPP__ */
