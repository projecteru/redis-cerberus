#include <cppformat/format.h>

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
    p->poll_add_ro(&this->_server);
    p->poll_add_ro(this);
    LOG(DEBUG) << "Start subscription " << this->str();
}

void Subscription::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(&this->_server);
        delete this;
    }
}

std::string Subscription::str() const
{
    return fmt::format("SubsCli({}@{})=S({}@{})", this->fd, static_cast<void const*>(this),
                       this->_server.fd, static_cast<void const*>(&this->_server));
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
            LOG(ERROR) << "Read 0 byte on " << this->str();
            return this->on_error();
        }
        b.write(this->_peer->fd);
    }
    if (poll::event_is_write(events)) {
        LOG(DEBUG) << "UNEXPECTED write on " << this->str();
        this->on_error();
    }
}

void Subscription::ServerConn::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(this->_peer);
        delete this->_peer;
    }
}

std::string Subscription::ServerConn::str() const
{
    return fmt::format("SubsSvr({}@{})=C({}@{})", this->fd, static_cast<void const*>(this),
                       this->_peer->fd, static_cast<void const*>(this->_peer));
}

BlockedListPop::BlockedListPop(Proxy* p, int clientfd, Server* peer, Buffer cmd)
    : LongConnection(clientfd, peer)
    , _server(peer->addr, std::move(cmd), this)
    , _proxy(p)
{
    p->poll_add_ro(&this->_server);
    p->poll_add_ro(this);
    LOG(DEBUG) << "Start blocked pop " << this->str();
}

void BlockedListPop::after_events(std::set<Connection*>& active_conns)
{
    if (this->closed()) {
        active_conns.erase(&this->_server);
        delete this;
    }
}

std::string BlockedListPop::str() const
{
    return fmt::format("BlkCli({}@{})=S({}@{})", this->fd, static_cast<void const*>(this),
                       this->_server.fd, static_cast<void const*>(&this->_server));
}

void BlockedListPop::restore_client(Buffer const& rsp, bool update_slot_map)
{
    if (this->closed()) {
        return;
    }
    rsp.write(this->fd);
    LOG(DEBUG) << "Restore to normal client " << this->str();
    this->_proxy->poll_del(this);
    this->_proxy->new_client(this->fd);
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
            LOG(ERROR) << "Read 0 byte on " << this->str();
            return this->on_error();
        }
        auto responses(split_server_response(this->_buffer));
        if (responses.empty()) {
            return;
        }
        if (responses[0]->server_moved()) {
            LOG(DEBUG) << "Server moved pop connection " << this->str();
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
        active_conns.erase(this->_peer);
        delete this->_peer;
    }
}

std::string BlockedListPop::ServerConn::str() const
{
    return fmt::format("BlkSvr({}@{})=C({}@{})", this->fd, static_cast<void const*>(this),
                       this->_peer->fd, static_cast<void const*>(this->_peer));
}
