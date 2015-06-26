#include <algorithm>

#include "client.hpp"
#include "proxy.hpp"
#include "server.hpp"
#include "except/exceptions.hpp"
#include "utils/logging.hpp"
#include "syscalls/poll.h"

using namespace cerb;

Client::Client(int fd, Proxy* p)
    : ProxyConnection(fd)
    , _proxy(p)
    , _awaiting_count(0)
{
    poll::poll_add_read(p->epfd, this->fd, this);
}

Client::~Client()
{
    for (Server* svr: this->_peers) {
        svr->pop_client(this);
    }
    this->_proxy->pop_client(this);
}

void Client::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        return this->close();
    }
    try {
        if (poll::event_is_read(events)) {
            this->_read_request();
        }
        if (this->closed()) {
            return;
        }
        if (poll::event_is_write(events)) {
            this->_write_response();
        }
    } catch (BadRedisMessage& e) {
        LOG(ERROR) << "Receive bad message from client " << this->fd
                   << " because: " << e.what();
        LOG(DEBUG) << "Dump buffer (before close): "
                   << this->_buffer.to_string();
        return this->close();
    }
}

void Client::after_events(std::set<Connection*>&)
{
    if (this->closed()) {
        delete this;
    }
}

void Client::_send_buffer_set()
{
    if (this->_output_buffer_set.writev(this->fd)) {
        for (auto const& g: this->_ready_groups) {
            g->collect_stats(this->_proxy);
        }
        this->_ready_groups.clear();
        this->_peers.clear();
        if (this->_parsed_groups.empty()) {
            poll::poll_read(this->_proxy->epfd, this->fd, this);
        } else {
            _process();
        }
        return;
    }
    poll::poll_write(this->_proxy->epfd, this->fd, this);
}

void Client::_write_response()
{
    if (!this->_output_buffer_set.empty()) {
        this->_send_buffer_set();
    }
    if (this->_awaiting_groups.empty() || _awaiting_count != 0) {
        return;
    }
    if (!this->_ready_groups.empty()) {
        LOG(DEBUG) << "-busy";
        return;
    }

    this->_ready_groups = std::move(this->_awaiting_groups);
    for (auto const& g: this->_ready_groups) {
        g->append_buffer_to(this->_output_buffer_set);
    }
    this->_send_buffer_set();
}

void Client::_read_request()
{
    int n = this->_buffer.read(this->fd);
    LOG(DEBUG) << "-read from " << this->fd << " current buffer size: " << this->_buffer.size() << " read returns " << n;
    if (n == 0) {
        return this->close();
    }
    ::split_client_command(this->_buffer, util::mkref(*this));
    if (this->_awaiting_groups.empty() && this->_ready_groups.empty()) {
        this->_process();
    } else {
        poll::poll_read(this->_proxy->epfd, this->fd, this);
    }
}

void Client::reactivate(util::sref<Command> cmd)
{
    Server* s = cmd->select_server(this->_proxy);
    if (s == nullptr) {
        return;
    }
    LOG(DEBUG) << "reactivated " << s->fd;
    poll::poll_write(this->_proxy->epfd, s->fd, s);
}

void Client::_process()
{
    for (auto& g: this->_parsed_groups) {
        if (g->long_connection()) {
            poll::poll_del(this->_proxy->epfd, this->fd);
            g->deliver_client(this->_proxy);
            LOG(DEBUG) << "Convert self to long connection, delete " << this;
            return this->close();
        }

        if (g->wait_remote()) {
            ++this->_awaiting_count;
            g->select_remote(this->_proxy);
        }
        this->_awaiting_groups.push_back(std::move(g));
    }
    this->_parsed_groups.clear();

    if (0 < this->_awaiting_count) {
        for (Server* svr: this->_peers) {
            poll::poll_write(this->_proxy->epfd, svr->fd, svr);
        }
        poll::poll_read(this->_proxy->epfd, this->fd, this);
    } else {
        this->_response_ready();
    }
    LOG(DEBUG) << "Processed, rest buffer " << this->_buffer.size();
}

void Client::_response_ready()
{
    if (this->closed()) {
        return;
    }
    if (this->_awaiting_groups.empty() && this->_ready_groups.empty()) {
        return poll::poll_read(this->_proxy->epfd, this->fd, this);
    }
    poll::poll_write(this->_proxy->epfd, this->fd, this);
}

void Client::group_responsed()
{
    if (--_awaiting_count == 0) {
        _response_ready();
    }
}

void Client::add_peer(Server* svr)
{
    this->_peers.insert(svr);
}

void Client::push_command(util::sptr<CommandGroup> g)
{
    this->_parsed_groups.push_back(std::move(g));
}
