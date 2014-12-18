#include <unistd.h>
#include <sys/epoll.h>

#include "subscription.hpp"
#include "exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

Subscription::Subscription(Proxy* p, int clientfd, util::Address const& addr,
                           Buffer subs_cmd)
    : ProxyConnection(clientfd)
    , _server(addr, std::move(subs_cmd), this)
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;

    ev.data.ptr = &_server;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, _server.fd, &ev) == -1) {
        throw SystemError("epoll_ctl+addi subscribe server", errno);
    }
    ev.data.ptr = this;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+addi subscribe client", errno);
    }

    LOG(DEBUG) << "Start subscription from " << addr.host << ':' << addr.port
               << " (FD=" << _server.fd << ") to client (FD=" << this->fd
               << ") by " << this;
}

void Subscription::triggered(int events)
{
    if (events & EPOLLRDHUP) {
        return this->close();
    }
    if (events & EPOLLIN) {
        Buffer b;
        if (b.read(fd) == 0) {
            LOG(DEBUG) << "Client quit because read 0 bytes";
            return this->close();
        }
    }
    if (events & EPOLLOUT) {
        LOG(DEBUG) << "UNEXPECTED";
        this->close();
    }
}

void Subscription::event_handled(std::set<Connection*>& active_conns)
{
    if (this->_closed) {
        active_conns.erase(&_server);
        delete this;
    }
}

Subscription::ServerConn::ServerConn(util::Address const& addr,
                                     Buffer subs_cmd, Subscription* peer)
    : ProxyConnection(new_stream_socket())
    , _peer(peer)
{
    set_nonblocking(this->fd);
    connect_fd(addr.host, addr.port, this->fd);
    subs_cmd.write(this->fd);
}

void Subscription::ServerConn::triggered(int events)
{
    if (events & EPOLLRDHUP) {
        return this->close();
    }
    if (events & EPOLLIN) {
        Buffer b;
        if (b.read(this->fd) == 0) {
            LOG(ERROR) << "Server closed subscription connection " << this->fd;
            return this->close();
        }
        b.write(this->_peer->fd);
    }
    if (events & EPOLLOUT) {
        LOG(DEBUG) << "UNEXPECTED";
        this->close();
    }
}

void Subscription::ServerConn::event_handled(std::set<Connection*>& active_conns)
{
    if (this->_closed) {
        active_conns.erase(_peer);
        delete _peer;
    }
}
