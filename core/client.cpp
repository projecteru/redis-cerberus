#include <algorithm>
#include <sys/epoll.h>

#include "client.hpp"
#include "proxy.hpp"
#include "server.hpp"
#include "exceptions.hpp"
#include "utils/logging.hpp"

using namespace cerb;

Client::~Client()
{
    std::for_each(this->_peers.begin(), this->_peers.end(),
                  [this](Server* svr)
                  {
                      svr->pop_client(this);
                  });
    _proxy->pop_client(this);
}

void Client::on_events(int events)
{
    if (events & EPOLLRDHUP) {
        return this->close();
    }
    try {
        if (events & EPOLLIN) {
            this->_read_request();
        }
        if (this->closed()) {
            return;
        }
        if (events & EPOLLOUT) {
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

void Client::_write_response()
{
    if (this->_awaiting_groups.empty() || _awaiting_count != 0) {
        return;
    }
    if (!this->_ready_groups.empty()) {
        LOG(DEBUG) << "-busy";
        return;
    }

    std::vector<util::sref<Buffer>> buffer_arr;
    this->_ready_groups = std::move(this->_awaiting_groups);
    for (auto const& g: this->_ready_groups) {
        g->append_buffer_to(buffer_arr);
    }
    Buffer::writev(this->fd, buffer_arr);
    for (auto const& g: this->_ready_groups) {
        g->collect_stats(this->_proxy);
    }
    this->_ready_groups.clear();
    this->_peers.clear();

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl-modi", errno);
    }

    if (!this->_parsed_groups.empty()) {
        _process();
    }
}

void Client::_read_request()
{
    int n = this->_buffer.read(this->fd);
    LOG(DEBUG) << "-read from " << this->fd << " current buffer size: " << this->_buffer.size() << " read returns " << n;
    if (n == 0) {
        return this->close();
    }

    split_client_command(this->_buffer, util::mkref(*this));

    if (_awaiting_groups.empty() && _ready_groups.empty()) {
        _process();
    }
}

void Client::reactivate(util::sref<Command> cmd)
{
    Server* s = cmd->select_server(this->_proxy);
    if (s == nullptr) {
        return;
    }
    LOG(DEBUG) << "reactivated " << s->fd;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = s;
    if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD, s->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modio Client::reactivate", errno);
    }
}

void Client::_process()
{
    for (auto& g: this->_parsed_groups) {
        if (g->long_connection()) {
            epoll_ctl(this->_proxy->epfd, EPOLL_CTL_DEL, this->fd, nullptr);
            g->deliver_client(this->_proxy);
            LOG(DEBUG) << "Convert self to long connection, delete " << this;
            return this->close();
        }

        if (g->wait_remote()) {
            ++_awaiting_count;
            g->select_remote(this->_proxy);
        }
        _awaiting_groups.push_back(std::move(g));
    }
    this->_parsed_groups.clear();

    if (0 < _awaiting_count) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        for (Server* svr: this->_peers) {
            ev.data.ptr = svr;
            if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD, svr->fd, &ev) == -1) {
                throw SystemError("epoll_ctl+modio Client::_process", errno);
            }
        }
    } else {
        _response_ready();
    }
    LOG(DEBUG) << "Processed, rest buffer " << this->_buffer.size();
}

void Client::_response_ready()
{
    if (this->closed()) {
        return;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl-modio", errno);
    }
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
