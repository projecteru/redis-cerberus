#include <algorithm>
#include <cppformat/format.h>

#include "client.hpp"
#include "proxy.hpp"
#include "server.hpp"
#include "except/exceptions.hpp"
#include "utils/logging.hpp"
#include "syscalls/poll.h"

using namespace cerb;

static msize_t const MAX_PIPE = 64;
static msize_t const MAX_RESPONSES = 256;

Client::Client(int fd, Proxy* p)
    : ProxyConnection(fd)
    , _proxy(p)
    , _awaiting_count(0)
{
    p->poll_add_ro(this);
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
        if (this->_output_buffer_set.empty()) {
            this->_proxy->set_conn_poll_ro(this);
        } else {
            this->_proxy->set_conn_poll_rw(this);
        }
    } catch (BadRedisMessage& e) {
        LOG(DEBUG) << fmt::format("Receive bad message from {} because {}", this->str(), e.what());
        LOG(DEBUG) << "Dump buffer " << this->_buffer.to_string();
        return this->close();
    } catch (IOErrorBase& e) {
        LOG(DEBUG) << "IOError: " << e.what() << " :: Close " << this->str();
        return this->close();
    }
}

void Client::after_events(std::set<Connection*>&)
{
    if (this->closed()) {
        delete this;
    }
}

std::string Client::str() const
{
    return fmt::format("Client({}@{})", this->fd, static_cast<void const*>(this));
}

void Client::_send_buffer_set()
{
    if (this->_output_buffer_set.writev(this->fd)) {
        for (auto const& g: this->_ready_groups) {
            g->collect_stats(this->_proxy);
        }
        this->_ready_groups.clear();
        this->_peers.clear();
        if (!this->_parsed_groups.empty()) {
            _process();
        }
    }
}

void Client::_push_awaitings_to_ready()
{
    if (this->_awaiting_count != 0 || (
            !this->_ready_groups.empty() &&
            this->_awaiting_groups.size() + this->_ready_groups.empty() > MAX_RESPONSES
        ))
    {
        return;
    }
    for (util::sptr<CommandGroup>& g: this->_awaiting_groups) {
        g->append_buffer_to(this->_output_buffer_set);
        this->_ready_groups.push_back(std::move(g));
    }
    this->_awaiting_groups.clear();
    if (!this->_output_buffer_set.empty()) {
        this->_proxy->set_conn_poll_rw(this);
    }
}

void Client::_write_response()
{
    if (!this->_output_buffer_set.empty()) {
        this->_send_buffer_set();
    }
    if (this->_awaiting_groups.empty() || _awaiting_count != 0) {
        return;
    }
    this->_push_awaitings_to_ready();
    this->_send_buffer_set();
}

void Client::_read_request()
{
    int n = this->_buffer.read(this->fd);
    LOG(DEBUG) << "Read from " << this->str() << " current buffer size: "
               << this->_buffer.size() << " read returns " << n;
    if (n == 0) {
        return this->close();
    }
    ::split_client_command(this->_buffer, util::mkref(*this));
    if (this->_awaiting_groups.empty()) {
        this->_process();
    }
}

void Client::reactivate(util::sref<Command> cmd)
{
    Server* s = cmd->select_server(this->_proxy);
    if (s == nullptr) {
        return;
    }
    LOG(DEBUG) << "reactivated " << s->str();
    this->_proxy->set_conn_poll_rw(s);
}

void Client::_process()
{
    msize_t pipe_groups = std::min(msize_t(this->_parsed_groups.size()), MAX_PIPE);
    LOG(DEBUG) << fmt::format("{} Process {} over {} commands", this->str(), pipe_groups, this->_parsed_groups.size());
    for (msize_t i = 0; i < pipe_groups; ++i) {
        auto& g = this->_parsed_groups[i];
        if (g->long_connection()) {
            this->_proxy->poll_del(this);
            g->deliver_client(this->_proxy);
            LOG(DEBUG) << "Convert self to long connection, close " << this->str();
            return this->close();
        }

        if (g->wait_remote()) {
            ++this->_awaiting_count;
            g->select_remote(this->_proxy);
        }
        this->_awaiting_groups.push_back(std::move(g));
    }
    if (pipe_groups == this->_parsed_groups.size()) {
        this->_parsed_groups.clear();
    } else {
        this->_parsed_groups.erase(this->_parsed_groups.begin(),
                                   this->_parsed_groups.begin() + pipe_groups);
    }

    if (0 < this->_awaiting_count) {
        for (Server* svr: this->_peers) {
            this->_proxy->set_conn_poll_rw(svr);
        }
    } else {
        this->_push_awaitings_to_ready();
    }
    LOG(DEBUG) << "Processed, rest buffer " << this->_buffer.size();
}

void Client::group_responsed()
{
    --this->_awaiting_count;
    this->_push_awaitings_to_ready();
}

void Client::add_peer(Server* svr)
{
    this->_peers.insert(svr);
}

void Client::push_command(util::sptr<CommandGroup> g)
{
    this->_parsed_groups.push_back(std::move(g));
}
