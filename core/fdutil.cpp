#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "fdutil.hpp"
#include "exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

FDWrapper::~FDWrapper()
{
    this->close();
}

bool FDWrapper::closed() const
{
    return this->fd == -1;
}

void FDWrapper::close()
{
    if (!this->closed()) {
        LOG(DEBUG) << "*close " << fd;
        ::close(this->fd);
        this->fd = -1;
    }
}

void cerb::set_nonblocking(int sockfd) {
    int opts;

    opts = fcntl(sockfd, F_GETFL);
    if (opts < 0) {
        throw SystemError("fcntl(F_GETFL)", errno);
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(sockfd, F_SETFL, opts) < 0) {
        throw SystemError("fcntl(set nonblocking)", errno);
    }
}

int cerb::set_tcpnodelay(int sockfd)
{
    int nodelay = 1;
    socklen_t len = sizeof nodelay;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, len);
}

int cerb::new_stream_socket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw SocketCreateError("Server create", errno);
    }
    return fd;
}

void cerb::connect_fd(std::string const& host, int port, int fd)
{
    set_tcpnodelay(fd);

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) != 1) {
        throw UnknownHost(host);
    }
    serv_addr.sin_port = htons(port);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&serv_addr),
                sizeof serv_addr) < 0)
    {
        if (errno == EINPROGRESS) {
            LOG(DEBUG) << "+connect in progress " << fd;
            return;
        }
        throw ConnectionRefused(host, port, errno);
    }
    LOG(DEBUG) << "+connect " << fd;
}

void cerb::bind_to(int fd, int port)
{
    int option = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
                   &option, sizeof option) < 0)
    {
        throw SystemError("set reuseport", errno);
    }
    struct sockaddr_in local;
    bzero(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&local), sizeof local) < 0)
    {
        throw SystemError("bind", errno);
    }
    ::listen(fd, 20);
}
