#include "subscription.hpp"
#include "server.hpp"
#include "client.hpp"
#include "response.hpp"
#include "utils/logging.hpp"
#include "syscalls/poll.h"
#include "syscalls/fctl.h"

using namespace cerb;

LongConnection::LongConnection(int clientfd, Server* svr)
    : ProxyConnection(clientfd)
    , _attached_server(svr)
{
    _attached_server->attach_long_connection(this);
}

LongConnection::~LongConnection()
{
    _attached_server->detach_long_connection(this);
}

void LongConnection::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        return this->on_error();
    }
    if (poll::event_is_read(events)) {
        Buffer b;
        if (b.read(this->fd) == 0) {
            LOG(DEBUG) << "Client quit because read 0 bytes";
            return this->on_error();
        }
    }
    if (poll::event_is_write(events)) {
        LOG(DEBUG) << "UNEXPECTED";
        this->on_error();
    }
}

Subscription::Subscription(Proxy* p, int clientfd, Server* peer, Buffer subs_cmd)
    : LongConnection(clientfd, peer)
    , _server(peer->addr, std::move(subs_cmd), this)
{
    poll::poll_add_read(p->epfd, this->_server.fd, &this->_server);
    poll::poll_add_read(p->epfd, this->fd, this);
    LOG(DEBUG) << "Start subscription from " << peer->addr.str()
               << " (FD=" << _server.fd << ") to client (FD=" << this->fd
               << ") by " << this;
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

BlockedListPop::BlockedListPop(Proxy* p, int clientfd, Server* peer, Buffer cmd)
    : LongConnection(clientfd, peer)
    , _server(peer->addr, std::move(cmd), this)
    , _proxy(p)
{
    poll::poll_add_read(p->epfd, this->_server.fd, &this->_server);
    poll::poll_add_read(p->epfd, this->fd, this);
    LOG(DEBUG) << "Start blocked pop from " << peer->addr.str()
               << " (FD=" << _server.fd << ") to client (FD=" << this->fd
               << ") by " << this;
}

void BlockedListPop::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(&_server);
        delete this;
    }
}

void BlockedListPop::restore_client(Buffer const& rsp, bool update_slot_map)
{
    if (this->closed()) {
        return;
    }
    rsp.write(this->fd);
    LOG(DEBUG) << "Restore FD " << this->fd;
    poll::poll_del(this->_proxy->epfd, this->fd);
    new Client(this->fd, this->_proxy);
    this->fd = -1;
    if (update_slot_map) {
        this->_proxy->update_slot_map();
    }
}

BlockedListPop::ServerConn::ServerConn(util::Address const& addr,
                                       Buffer subs_cmd, BlockedListPop* peer)
    : ProxyConnection(fctl::new_stream_socket())
    , _peer(peer)
{
    fctl::set_nonblocking(this->fd);
    fctl::connect_fd(addr.host, addr.port, this->fd);
    subs_cmd.write(this->fd);
}

void BlockedListPop::ServerConn::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        return this->on_error();
    }
    if (poll::event_is_read(events)) {
        if (this->_buffer.read(this->fd) == 0) {
            LOG(ERROR) << "Server closed pop connection " << this->fd;
            return this->on_error();
        }
        auto responses(split_server_response(this->_buffer));
        if (responses.empty()) {
            return;
        }
        if (responses[0]->server_moved()) {
            LOG(DEBUG) << "Server moved pop connection " << this->fd;
            this->_peer->restore_client(Response::NIL, true);
        } else {
            this->_peer->restore_client(responses[0]->get_buffer(), false);
        }
    }
    if (poll::event_is_write(events)) {
        LOG(DEBUG) << "UNEXPECTED";
        this->on_error();
    }
}

void BlockedListPop::ServerConn::on_error()
{
    this->close();
    this->_peer->restore_client(Response::NIL, true);
}

void BlockedListPop::ServerConn::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(_peer);
        delete _peer;
    }
}
