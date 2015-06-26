#include "subscription.hpp"
#include "server.hpp"
#include "utils/logging.hpp"
#include "syscalls/poll.h"
#include "syscalls/fctl.h"

using namespace cerb;

Subscription::Subscription(Proxy* p, int clientfd, Server* peer, Buffer subs_cmd)
    : ProxyConnection(clientfd)
    , _server(peer->addr, std::move(subs_cmd), this)
    , _peer(peer)
{
    _peer->attach_long_connection(this);
    poll::poll_add_read(p->epfd, this->_server.fd, &this->_server);
    poll::poll_add_read(p->epfd, this->fd, this);
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
    if (poll::event_is_hup(events)) {
        return this->on_error();
    }
    if (poll::event_is_read(events)) {
        Buffer b;
        if (b.read(fd) == 0) {
            LOG(DEBUG) << "Client quit because read 0 bytes";
            return this->on_error();
        }
    }
    if (poll::event_is_write(events)) {
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
    : ProxyConnection(fctl::new_stream_socket())
    , _peer(peer)
{
    fctl::set_nonblocking(this->fd);
    fctl::connect_fd(addr.host, addr.port, this->fd);
    subs_cmd.write(this->fd);
}

void Subscription::ServerConn::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        return this->on_error();
    }
    if (poll::event_is_read(events)) {
        Buffer b;
        if (b.read(this->fd) == 0) {
            LOG(ERROR) << "Server closed subscription connection " << this->fd;
            return this->on_error();
        }
        b.write(this->_peer->fd);
    }
    if (poll::event_is_write(events)) {
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
