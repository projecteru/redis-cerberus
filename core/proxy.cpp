#include <unistd.h>
#include <netdb.h>
#include <sys/epoll.h>

#include "proxy.hpp"
#include "response.hpp"
#include "exceptions.hpp"
#include "utils/string.h"
#include "utils/logging.hpp"

using namespace cerb;

static int const MAX_EVENTS = 1024;

void ProxyConnection::event_handled(std::set<Connection*>&)
{
    if (this->_closed) {
        delete this;
    }
}

void ProxyConnection::close()
{
    this->_closed = true;
}

void Acceptor::triggered(int)
{
    _proxy->accept_from(this->fd);
}

void Acceptor::close()
{
    LOG(ERROR) << "Accept error.";
}

Server::Server(std::string const& host, int port, Proxy* p)
    : ProxyConnection(new_stream_socket())
    , _proxy(p)
{
    set_nonblocking(fd);
    connect_fd(host, port, this->fd);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw SystemError("epoll_ctl+add", errno);
    }
}

void Server::triggered(int events)
{
    if (events & EPOLLRDHUP) {
        return this->close();
    }
    if (events & EPOLLIN) {
        try {
            this->_recv_from();
        } catch (BadRedisMessage& e) {
            LOG(FATAL) << "Receive bad message from server " << this->fd
                       << " because: " << e.what()
                       << " dump buffer (before close): "
                       << this->_buffer.to_string();
            exit(1);
        }
    }
    if (events & EPOLLOUT) {
        this->_send_to();
    }
}

void Server::event_handled(std::set<Connection*>&)
{
    if (this->_closed) {
        LOG(ERROR) << "Server closed connection " << this->fd
                   << ". Notify proxy to update slot map";
        ::close(this->fd);
        this->fd = -1;
        _proxy->server_closed();
    }
}

void Server::_send_to()
{
    if (this->_commands.empty()) {
        return;
    }
    if (!this->_ready_commands.empty()) {
        LOG(DEBUG) << "+busy";
        return;
    }

    std::vector<struct iovec> iov;
    int total_bytes = 0;

    this->_ready_commands = std::move(this->_commands);
    std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                  [&](util::sref<Command>& cmd)
                  {
                      cmd->buffer.buffer_ready(iov);
                      total_bytes += cmd->buffer.size();
                  });
    cerb::writev(this->fd, total_bytes, iov);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modi", errno);
    }
}

void Server::_recv_from()
{
    int n = this->_buffer.read(this->fd);
    if (n == 0) {
        LOG(INFO) << "Server hang up: " << this->fd;
        throw ConnectionHungUp();
    }
    LOG(DEBUG) << "+read from " << this->fd
               << " buffer size " << this->_buffer.size()
               << ": " << this->_buffer.to_string();
    auto responses(split_server_response(this->_buffer));
    if (responses.size() > this->_ready_commands.size()) {
        LOG(ERROR) << "+Error on split, expected size: " << this->_ready_commands.size()
                   << " actual: " << responses.size() << " dump buffer:";
        std::for_each(responses.begin(), responses.end(),
                      [](util::sptr<Response> const& rsp)
                      {
                          LOG(ERROR) << "::: " << rsp->dump_buffer().to_string();
                      });
        LOG(ERROR) << "Rest buffer: " << this->_buffer.to_string();
        LOG(FATAL) << "Exit";
        exit(1);
    }
    LOG(DEBUG) << "+responses size: " << responses.size();
    LOG(DEBUG) << "+rest buffer: " << this->_buffer.size() << ": " << this->_buffer.to_string();
    auto client_it = this->_ready_commands.begin();
    std::for_each(responses.begin(), responses.end(),
                  [&](util::sptr<Response>& rsp)
                  {
                      util::sref<Command> c = *client_it++;
                      if (c.not_nul()) {
                          rsp->rsp_to(c, util::mkref(*this->_proxy));
                      }
                  });
    this->_ready_commands.erase(this->_ready_commands.begin(), client_it);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modio Server::_recv_from", errno);
    }
}

void Server::push_client_command(util::sref<Command> cmd)
{
    _commands.push_back(cmd);
}

void Server::pop_client(Client* cli)
{
    std::remove_if(this->_commands.begin(), this->_commands.end(),
                   [&](util::sref<Command>& cmd)
                   {
                       return cmd->group->client.is(cli);
                   });
    std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                  [&](util::sref<Command>& cmd)
                  {
                      if (cmd.not_nul() && cmd->group->client.is(cli)) {
                          cmd.reset();
                      }
                  });
}

std::vector<util::sref<Command>> Server::deliver_commands()
{
    std::remove_if(this->_ready_commands.begin(), this->_ready_commands.end(),
                   [&](util::sref<Command> cmd)
                   {
                       return cmd.nul();
                   });
    _commands.insert(_commands.end(), _ready_commands.begin(),
                     _ready_commands.end());
    return std::move(_commands);
}

SlotsMapUpdater::SlotsMapUpdater(util::Address const& addr, Proxy* p)
    : Connection(new_stream_socket())
    , _proxy(p)
{
    set_nonblocking(fd);
    connect_fd(addr.host, addr.port, this->fd);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw SystemError("epoll_ctl#add", errno);
    }
}

void SlotsMapUpdater::_await_data()
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl#modi", errno);
    }
}

void SlotsMapUpdater::_send_cmd()
{
    write_slot_map_cmd_to(this->fd);
    this->_await_data();
}

void SlotsMapUpdater::_recv_rsp()
{
    _rsp.read(this->fd);
    LOG(DEBUG) << "+read slots from " << this->fd
               << " buffer size " << this->_rsp.size()
               << ": " << this->_rsp.to_string();
    std::vector<util::sptr<Response>> rsp(split_server_response(_rsp));
    if (rsp.size() == 0) {
        return this->_await_data();
    }
    if (rsp.size() != 1) {
        throw BadRedisMessage("Ask cluster nodes returns responses with size=" +
                              util::str(int(rsp.size())));
    }
    this->_updated_map = std::move(
        parse_slot_map(rsp[0]->dump_buffer().to_string()));
    _proxy->notify_slot_map_updated();
}

void SlotsMapUpdater::triggered(int events)
{
    if (events & EPOLLRDHUP) {
        LOG(ERROR) << "Failed to retrieve slot map from " << this->fd
                   << ". Closed because remote hung up.";
        throw ConnectionHungUp();
    }
    if (events & EPOLLIN) {
        try {
            this->_recv_rsp();
        } catch (BadRedisMessage& e) {
            LOG(FATAL) << "Receive bad message from server on update from "
                       << this->fd
                       << " because: " << e.what()
                       << " buffer length=" << this->_rsp.size()
                       << " dump buffer (before close): "
                       << this->_rsp.to_string();
            exit(1);
        }
    }
    if (events & EPOLLOUT) {
        this->_send_cmd();
    }
}

void SlotsMapUpdater::close()
{
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
    _proxy->notify_slot_map_updated();
}

Client::~Client()
{
    std::for_each(this->_peers.begin(), this->_peers.end(),
                  [&](Server* svr)
                  {
                      svr->pop_client(this);
                  });
    _proxy->pop_client(this);
}

void Client::triggered(int events)
{
    if (events & EPOLLRDHUP) {
        return this->close();
    }
    if (events & EPOLLIN) {
        try {
            this->_recv_from();
        } catch (BadRedisMessage& e) {
            LOG(ERROR) << "Receive bad message from client " << this->fd
                       << " because: " << e.what()
                       << " dump buffer (before close): "
                       << this->_buffer.to_string();
            return this->close();
        }
    }
    if (events & EPOLLOUT) {
        this->_send_to();
    }
}

void Client::_send_to()
{
    if (this->_awaiting_groups.empty() || _awaiting_count != 0) {
        return;
    }
    if (!this->_ready_groups.empty()) {
        LOG(DEBUG) << "-busy";
        return;
    }

    std::vector<struct iovec> iov;
    int n = 0, nwrite = -1;

    this->_ready_groups = std::move(this->_awaiting_groups);
    std::for_each(this->_ready_groups.begin(), this->_ready_groups.end(),
                  [&](util::sptr<CommandGroup>& g)
                  {
                      g->append_buffer_to(iov);
                      n += g->total_buffer_size();
                  });

    LOG(DEBUG) << "-write to " << this->fd << " total vector size: " << iov.size();
    while (true) {
        nwrite = writev(this->fd, iov.data(), iov.size());
        if (nwrite < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            throw IOError("-writev", errno);
        }
        break;
    }
    if (nwrite != n) {
        throw IOError("-writev (should recover)", errno);
    }
    this->_ready_groups.clear();
    this->_peers.clear();

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl-modi", errno);
    }

    if (!this->_buffer.empty()) {
        _process();
    }
}

void Client::_recv_from()
{
    int n = this->_buffer.read(this->fd);
    LOG(DEBUG) << "-read from " << this->fd << " current buffer size: " << this->_buffer.size() << " read returns " << n;
    if (n == 0) {
        return this->close();
    }
    if (!(_awaiting_groups.empty() && _ready_groups.empty())) {
        return;
    }
    _process();
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
    auto commands(split_client_command(this->_buffer, util::mkref(*this)));
    if (commands.size() == 1 && commands[0]->long_conn_command) {
        epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
        commands[0]->deliver_client(this->_proxy, this);
        LOG(DEBUG) << "Convert self to long connection, delete " << this;
        return this->close();
    }
    for (auto i = commands.begin(); i != commands.end(); ++i) {
        util::sptr<CommandGroup> g(std::move(*i));
        if (g->long_conn_command) {
            commands[0]->deliver_client(this->_proxy, this);
            return this->close();
        }

        if (g->awaiting_count != 0) {
            ++_awaiting_count;
            for (auto ci = g->commands.begin(); ci != g->commands.end(); ++ci) {
                Server* s = (*ci)->select_server(this->_proxy);
                if (s != nullptr) {
                    this->_peers.insert(s);
                }
            }
        }
        _awaiting_groups.push_back(std::move(g));
    }
    if (0 < _awaiting_count) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        std::for_each(this->_peers.begin(), this->_peers.end(),
                      [&](Server* svr)
                      {
                           ev.data.ptr = svr;
                           if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD,
                                         svr->fd, &ev) == -1)
                           {
                               throw SystemError(
                                   "epoll_ctl+modio Client::_process", errno);
                           }
                      });
    } else {
        _response_ready();
    }
    LOG(DEBUG) << "Processed, rest buffer " << this->_buffer.size();
}

void Client::_response_ready()
{
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

Proxy::Proxy(util::Address const& remote)
    : _clients_count(0)
    , _server_map([&](std::string const& host, int port)
                  {
                      return new Server(host, port, this);
                  })
    , _active_slot_updaters_count(0)
    , _server_closed(false)
    , epfd(epoll_create(MAX_EVENTS))
{
    if (epfd == -1) {
        throw std::runtime_error("epoll_create");
    }
    _slot_updaters.push_back(util::mkptr(new SlotsMapUpdater(remote, this)));
    ++_active_slot_updaters_count;
}

Proxy::~Proxy()
{
    close(epfd);
}

void Proxy::_update_slot_map()
{
    if (_slot_updaters.end() == std::find_if(
            _slot_updaters.begin(), _slot_updaters.end(),
            [&](util::sptr<SlotsMapUpdater>& updater)
            {
                if (!updater->success()) {
                    LOG(DEBUG) << "Discard a failed node";
                    return false;
                }
                std::map<slot, util::Address> m(updater->deliver_map());
                for (auto i = m.begin(); i != m.end(); ++i) {
                    if (i->second.host.empty()) {
                        LOG(DEBUG) << "Discard result because address is empty string "
                                   << ':' << i->second.port;
                        return false;
                    }
                }
                _set_slot_map(std::move(m));
                return true;
            }))
    {
        throw BadClusterStatus("Fail to update slot mapping");
    }
    _finished_slot_updaters = std::move(_slot_updaters);
    LOG(INFO) << "Slot map updated";
}

void Proxy::_set_slot_map(std::map<slot, util::Address> map)
{
    auto s(_server_map.set_map(std::move(map)));
    std::for_each(s.begin(), s.end(),
                  [&](Server* s)
                  {
                      std::vector<util::sref<Command>> c(s->deliver_commands());
                      _retrying_commands.insert(_retrying_commands.end(),
                                                c.begin(), c.end());
                      delete s;
                  });

    if (!_server_map.all_covered()) {
        LOG(ERROR) << "Map not covered all slots";
        return _retrieve_slot_map();
    }

    _server_closed = false;
    if (_retrying_commands.empty()) {
        return;
    }

    LOG(DEBUG) << "Retry MOVED or ASK: " << _retrying_commands.size();
    std::set<Server*> svrs;
    std::vector<util::sref<Command>> retrying(std::move(_retrying_commands));
    std::for_each(retrying.begin(), retrying.end(),
                  [&](util::sref<Command> cmd)
                  {
                      Server* s = cmd->select_server(this);
                      if (s == nullptr) {
                          return;
                      }
                      cmd->group->client->add_peer(s);
                      svrs.insert(s);
                  });

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    std::for_each(svrs.begin(), svrs.end(),
                  [&](Server* svr)
                  {
                      ev.data.ptr = svr;
                      if (epoll_ctl(this->epfd, EPOLL_CTL_MOD,
                                    svr->fd, &ev) == -1)
                      {
                          throw SystemError(
                              "epoll_ctl+modio Proxy::_set_slot_map", errno);
                      }
                  });
}

void Proxy::_retrieve_slot_map()
{
    _server_map.iterate_addr(
        [&](util::Address const& addr)
        {
            try {
                _slot_updaters.push_back(
                    util::mkptr(new SlotsMapUpdater(addr, this)));
                ++_active_slot_updaters_count;
            } catch (ConnectionRefused& e) {
                LOG(INFO) << e.what();
                LOG(INFO) << "Disconnected: " << addr.host << ':' << addr.port;
            } catch (UnknownHost& e) {
                LOG(ERROR) << e.what();
            }
        });
    if (_slot_updaters.empty()) {
        throw BadClusterStatus("No nodes could be reached");
    }
}

void Proxy::notify_slot_map_updated()
{
    if (--_active_slot_updaters_count == 0) {
        _update_slot_map();
    }
}

void Proxy::server_closed()
{
    _server_closed = true;
}

bool Proxy::_should_update_slot_map() const
{
    return _slot_updaters.empty() &&
        (!_retrying_commands.empty() || _server_closed);
}

void Proxy::retry_move_ask_command_later(util::sref<Command> cmd)
{
    _retrying_commands.push_back(cmd);
    LOG(DEBUG) << "A MOVED or ASK added for later retry - " << _retrying_commands.size();
}

void Proxy::run(int listen_port)
{
    cerb::Acceptor acc(new_stream_socket(), this);
    set_nonblocking(acc.fd);
    bind_to(acc.fd, listen_port);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &acc;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, acc.fd, &ev) == -1) {
        throw SystemError("epoll_ctl*listen", errno);
    }

    while (true) {
        this->_loop();
    }
}

void Proxy::_loop()
{
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(this->epfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
        if (errno == EINTR) {
            return;
        }
        throw SystemError("epoll_wait", errno);
    }

    std::set<Connection*> active_conns;
    std::set<Connection*> closed_conns;
    LOG(DEBUG) << "*epoll wait: " << nfds;
    for (int i = 0; i < nfds; ++i) {
        Connection* conn = static_cast<Connection*>(events[i].data.ptr);
        LOG(DEBUG) << "*epoll process " << conn->fd;
        if (closed_conns.find(conn) != closed_conns.end()) {
            continue;
        }
        active_conns.insert(conn);
        try {
            conn->triggered(events[i].events);
        } catch (IOErrorBase& e) {
            LOG(ERROR) << "IOError: " << e.what();
            LOG(ERROR) << "Close connection to " << conn->fd << " in " << conn;
            conn->close();
            closed_conns.insert(conn);
        }
    }
    LOG(DEBUG) << "*epoll done";
    std::for_each(active_conns.begin(), active_conns.end(),
                  [&](Connection* c)
                  {
                      c->event_handled(active_conns);
                  });
    _finished_slot_updaters.clear();
    if (this->_should_update_slot_map()) {
        LOG(DEBUG) << "Should update slot map";
        this->_retrieve_slot_map();
    }
}

Server* Proxy::get_server_by_slot(slot key_slot)
{
    Server* s = _server_map.get_by_slot(key_slot);
    return (s == nullptr || s->fd == -1) ? nullptr : s;
}

void Proxy::accept_from(int listen_fd)
{
    int cfd;
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof remote;
    while ((cfd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&remote),
                         &addrlen)) > 0)
    {
        LOG(DEBUG) << "*accept " << cfd;
        set_nonblocking(cfd);
        set_tcpnodelay(cfd);
        Connection* c = new Client(cfd, this);
        ++this->_clients_count;
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = c;
        if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
            throw SystemError("epoll_ctl-add", errno);
        }
    }
    if (cfd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED
            && errno != EPROTO && errno != EINTR)
        {
            throw SocketAcceptError(errno);
        }
    }
}

void Proxy::pop_client(Client* cli)
{
    std::remove_if(this->_retrying_commands.begin(),
                   this->_retrying_commands.end(),
                   [&](util::sref<Command> cmd)
                   {
                       return cmd->group->client.is(cli);
                   });
    --this->_clients_count;
}
