#include <climits>
#include <unistd.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <algorithm>

#include "proxy.hpp"
#include "response.hpp"
#include "exceptions.hpp"
#include "utils/string.h"
#include "utils/logging.hpp"

using namespace cerb;

static int const MAX_EVENTS = 1024;

void Acceptor::triggered(Proxy* p, int)
{
    p->accept_from(this->fd);
}

Server::Server(std::string const& host, int port, Proxy* p)
    : Connection(new_stream_socket())
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

Server::~Server()
{
    _proxy->shut_server(this);
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
}

void Server::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
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
    int n = 0, ntotal = 0, written_iov = 0;

    this->_ready_commands = std::move(this->_commands);
    std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                  [&](util::sref<Command>& cmd)
                  {
                      cmd->buffer.buffer_ready(iov);
                      n += cmd->buffer.size();
                  });
    LOG(DEBUG) << "+write to " << this->fd << " total vector size: " << iov.size();
    int rest_iov = iov.size();

    while (written_iov < int(iov.size())) {
        int iovcnt = std::min(rest_iov, IOV_MAX);
        int nwrite = writev(this->fd, iov.data() + written_iov, iovcnt);
        if (nwrite < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            throw IOError("+writev", errno);
        }
        ntotal += nwrite;
        rest_iov -= iovcnt;
        written_iov += iovcnt;
    }

    if (ntotal != n) {
        throw IOError("+writev (should recover)", errno);
    }

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
    LOG(DEBUG) << "+read from " << this->fd << " current buffer size: " << this->_buffer.size();
    if (n == 0) {
        return;
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
        LOG(FATAL) << "Exit";
        exit(1);
    }
    LOG(DEBUG) << "+responses size: " << responses.size();
    LOG(DEBUG) << "+rest buffer: " << this->_buffer.size() << ": " << this->_buffer.to_string();
    auto client_it = this->_ready_commands.begin();
    std::for_each(responses.begin(), responses.end(),
                  [&](util::sptr<Response>& rsp)
                  {
                      rsp->rsp_to(*client_it++, util::mkref(*this->_proxy));
                  });
    this->_ready_commands.erase(this->_ready_commands.begin(), client_it);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modio", errno);
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
                      if (cmd->group->client.is(cli)) {
                          cmd.reset();
                      }
                  });
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

void SlotsMapUpdater::_send_cmd()
{
    write_slot_map_cmd_to(this->fd);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl#modi", errno);
    }
}

void SlotsMapUpdater::_recv_rsp()
{
    _proxy->set_slot_map(read_slot_map_from(this->fd));
}

void SlotsMapUpdater::triggered(Proxy* p, int events)
{
    if (events & EPOLLRDHUP) {
        LOG(ERROR) << "Failed to retrieve slot map from " << this->fd
                   << ". Closed because remote hung up.";
        return p->retrieve_slot_map();
    }
    try {
        if (events & EPOLLIN) {
            this->_recv_rsp();
        }
        if (events & EPOLLOUT) {
            this->_send_cmd();
        }
    } catch (IOError& e) {
        LOG(ERROR) << "Failed to retrive slot map from " << this->fd
                   << ". Closed because: " << e.what();
        LOG(ERROR) << "Retry...";
        return p->retrieve_slot_map();
    }
}

Client::~Client()
{
    std::for_each(this->_peers.begin(), this->_peers.end(),
                  [&](Server* svr)
                  {
                      svr->pop_client(this);
                  });
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
}

void Client::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
    }
    try {
        if (events & EPOLLIN) {
            try {
                this->_recv_from();
            } catch (BadRedisMessage& e) {
                LOG(ERROR) << "Receive bad message from client " << this->fd
                           << " because: " << e.what()
                           << " dump buffer (before close): "
                           << this->_buffer.to_string();
                delete this;
                return;
            }
        }
        if (events & EPOLLOUT) {
            this->_send_to();
        }
    } catch (IOError& e) {
        LOG(ERROR) << "Client IO Error " << this->fd
                   << ". Closed because: " << e.what();
        delete this;
    }
}

void Client::_send_to()
{
    if (this->_awaiting_groups.empty()) {
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
    LOG(DEBUG) << "-read from " << this->fd << " current buffer size: " << this->_buffer.size();
    if (n == 0) {
        delete this;
        return;
    }
    if (!(_awaiting_groups.empty() && _ready_groups.empty())) {
        return;
    }
    _process();
}

void Client::_process()
{
    auto messages(split_client_command(this->_buffer, util::mkref(*this)));
    for (auto i = messages.begin(); i != messages.end(); ++i) {
        util::sptr<CommandGroup> g(std::move(*i));
        if (g->awaiting_count != 0) {
            ++_awaiting_count;
            for (auto ci = g->commands.begin(); ci != g->commands.end(); ++ci) {
                Server* svr = this->_proxy->get_server_by_slot((*ci)->key_slot);
                this->_peers.insert(svr);
                svr->push_client_command(**ci);
            }
        }
        _awaiting_groups.push_back(std::move(g));
    }
    this->_buffer.clear();
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
                               throw SystemError("epoll_ctl+modio", errno);
                           }
                      });
    } else {
        _response_ready();
    }
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
    : _server_map([&](std::string const& host, int port)
                  {
                      return new Server(host, port, this);
                  }, slot_map_from_remote(remote))
    , _slot_updater(nullptr)
    , epfd(epoll_create(MAX_EVENTS))
{
    if (epfd == -1) {
        throw std::runtime_error("epoll_create");
    }
}

Proxy::~Proxy()
{
    close(epfd);
}

void Proxy::set_slot_map(std::map<slot, util::Address> map)
{
    _slot_updater.reset();
    auto s(_server_map.set_map(std::move(map)));
    std::for_each(s.begin(), s.end(), [](Server* s) {delete s;});

    if (_move_ask_command.empty()) {
        return;
    }

    LOG(DEBUG) << "Retry MOVED or ASK: " << _move_ask_command.size();
    std::set<Server*> svrs;
    std::for_each(_move_ask_command.begin(), _move_ask_command.end(),
                  [&](util::sref<Command> cmd)
                  {
                      Server* svr = this->get_server_by_slot(cmd->key_slot);
                      cmd->group->client->add_peer(svr);
                      svr->push_client_command(cmd);
                      svrs.insert(svr);
                  });
    _move_ask_command.clear();

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    std::for_each(svrs.begin(), svrs.end(),
                  [&](Server* svr)
                  {
                      ev.data.ptr = svr;
                      if (epoll_ctl(this->epfd, EPOLL_CTL_MOD,
                                    svr->fd, &ev) == -1)
                      {
                          throw SystemError("epoll_ctl+modio update", errno);
                      }
                  });
}

void Proxy::retrieve_slot_map()
{
    if (!_server_map.iterate_addr_util(
        [&](util::Address const& addr)
        {
            try {
                _slot_updater.reset(new SlotsMapUpdater(addr, this));
                return true;
            } catch (ConnectionRefused& e) {
                LOG(INFO) << "Disconnected: " << addr.host << ':' << addr.port;
                return false;
            }
        }))
    {
        LOG(FATAL) << "No nodes could be reached";
        exit(1);
    }
    LOG(DEBUG) << "Retrieve slot map from " << _slot_updater->fd;
}

bool Proxy::_slot_map_not_updated() const
{
    return _slot_updater.nul() && !_move_ask_command.empty();
}

void Proxy::retry_move_ask_command_later(util::sref<Command> cmd)
{
    _move_ask_command.push_back(cmd);
    LOG(DEBUG) << "A MOVED or ASK added for later retry - " << _move_ask_command.size();
}

void Proxy::run(int listen_port)
{
    cerb::Acceptor acc(new_stream_socket());
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

    for (int i = 0; i < nfds; ++i) {
        try {
            Connection* conn = static_cast<Connection*>(events[i].data.ptr);
            conn->triggered(this, events[i].events);
        } catch (cerb::IOError& e) {
            LOG(FATAL) << " UNEXPECTED IOError " << e.what();
            exit(1);
        }
    }
    if (this->_slot_map_not_updated()) {
        LOG(DEBUG) << "Need update slot map";
        this->retrieve_slot_map();
    }
}

void Proxy::accept_from(int listen_fd)
{
    int cfd;
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof remote;
    while ((cfd = accept(listen_fd, (struct sockaddr*)&remote, &addrlen)) > 0) {
        LOG(DEBUG) << "*accept " << cfd;
        set_nonblocking(cfd);
        set_tcpnodelay(cfd);
        Connection* c = new Client(cfd, this);
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

void Proxy::shut_server(Server* svr)
{
    _server_map.erase_val(svr);
}
