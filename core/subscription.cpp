#include <sys/epoll.h>

#include "subscription.hpp"
#include "server.hpp"
#include "exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

Subscription::Subscription(Proxy* p, int clientfd, Server* peer, Buffer subs_cmd)
    : ProxyConnection(clientfd)
    , _server(peer->addr, std::move(subs_cmd), this)
    , _peer(peer)
{
    _peer->attach_long_connection(this);
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

    LOG(DEBUG) << "Start subscription from " << peer->addr.host << ':' << peer->addr.port
               << " (FD=" << _server.fd << ") to client (FD=" << this->fd
               << ") by " << this;
}

Subscription::~Subscription()
{
    _peer->detach_long_connection(this);
}

void Subscription::on_events(int events)
{
    if (events & EPOLLRDHUP) {
        return this->on_error();
    }
    if (events & EPOLLIN) {
        Buffer b;
        if (b.read(fd) == 0) {
            LOG(DEBUG) << "Client quit because read 0 bytes";
            return this->on_error();
        }
    }
    if (events & EPOLLOUT) {
        LOG(DEBUG) << "UNEXPECTED";
        this->on_error();
    }
}

void Subscription::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
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

void Subscription::ServerConn::on_events(int events)
{
    if (events & EPOLLRDHUP) {
        return this->on_error();
    }
    if (events & EPOLLIN) {
        Buffer b;
        if (b.read(this->fd) == 0) {
            LOG(ERROR) << "Server closed subscription connection " << this->fd;
            return this->on_error();
        }
        b.write(this->_peer->fd);
    }
    if (events & EPOLLOUT) {
        LOG(DEBUG) << "UNEXPECTED";
        this->on_error();
    }
}

void Subscription::ServerConn::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(_peer);
        delete _peer;
    }
}
