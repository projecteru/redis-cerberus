#include <sys/epoll.h>
#include <map>

#include "command.hpp"
#include "server.hpp"
#include "client.hpp"
#include "proxy.hpp"
#include "response.hpp"
#include "exceptions.hpp"
#include "utils/alg.hpp"
#include "utils/logging.hpp"

using namespace cerb;

void Server::on_events(int events)
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

void Server::_send_to()
{
    if (this->_commands.empty()) {
        return;
    }
    if (!this->_ready_commands.empty()) {
        LOG(DEBUG) << "+busy";
        return;
    }

    std::vector<util::sref<Buffer>> buffer_arr;
    this->_ready_commands = std::move(this->_commands);
    buffer_arr.reserve(this->_ready_commands.size());
    for (util::sref<DataCommand> c: this->_ready_commands) {
        buffer_arr.push_back(util::mkref(c->buffer));
    }
    Buffer::writev(this->fd, buffer_arr);
    auto now = Clock::now();
    for (util::sref<DataCommand> c: this->_ready_commands) {
        c->sent_time = now;
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
        for (util::sptr<Response> const& rsp: responses) {
            LOG(ERROR) << "::: " << rsp->dump_buffer().to_string();
        }
        LOG(ERROR) << "Rest buffer: " << this->_buffer.to_string();
        LOG(FATAL) << "Exit";
        exit(1);
    }
    LOG(DEBUG) << "+responses size: " << responses.size();
    LOG(DEBUG) << "+rest buffer: " << this->_buffer.size() << ": " << this->_buffer.to_string();
    auto cmd_it = this->_ready_commands.begin();
    auto now = Clock::now();
    for (util::sptr<Response>& rsp: responses) {
        util::sref<DataCommand> c = *cmd_it++;
        if (c.not_nul()) {
            rsp->rsp_to(c, util::mkref(*this->_proxy));
            c->resp_time = now;
        }
    }
    this->_ready_commands.erase(this->_ready_commands.begin(), cmd_it);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modio Server::_recv_from", errno);
    }
}

void Server::push_client_command(util::sref<DataCommand> cmd)
{
    _commands.push_back(cmd);
    cmd->group->client->add_peer(this);
}

void Server::pop_client(Client* cli)
{
    util::erase_if(
        this->_commands,
        [&](util::sref<DataCommand> cmd)
        {
            return cmd->group->client.is(cli);
        });
    for (util::sref<DataCommand>& cmd: this->_ready_commands) {
        if (cmd.not_nul() && cmd->group->client.is(cli)) {
            cmd.reset();
        }
    }
}

std::vector<util::sref<DataCommand>> Server::deliver_commands()
{
    util::erase_if(
        this->_ready_commands,
        [](util::sref<DataCommand> cmd)
        {
            return cmd.nul();
        });
    _commands.insert(_commands.end(), _ready_commands.begin(),
                     _ready_commands.end());
    return std::move(_commands);
}

static thread_local std::map<util::Address, Server*> servers_map;
static thread_local std::vector<Server*> servers_pool;

static void remove_entry(Server* server)
{
    servers_map.erase(server->addr);
}

void Server::after_events(std::set<Connection*>&)
{
    if (this->closed()) {
        LOG(ERROR) << "Server closed connection " << this->fd
                   << ". Notify proxy to update slot map";
        _proxy->update_slot_map();
    }
}

std::map<util::Address, Server*>::iterator Server::addr_begin()
{
    return servers_map.begin();
}

std::map<util::Address, Server*>::iterator Server::addr_end()
{
    return servers_map.end();
}

static std::function<void(int, std::vector<util::sref<DataCommand>>&)> on_server_connected(
    [](int, std::vector<util::sref<DataCommand>>&) {});

void Server::_reconnect(util::Address const& addr, Proxy* p)
{
    this->fd = new_stream_socket();
    this->_proxy = p;
    this->addr = addr;

    set_nonblocking(this->fd);
    LOG(DEBUG) << "Connecting to " << addr.host << ':' << addr.port << " for " << fd << " from " << this;
    connect_fd(addr.host, addr.port, this->fd);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw SystemError("epoll_ctl+add", errno);
    }

    ::on_server_connected(this->fd, this->_ready_commands);
}

Server* Server::_alloc_server(util::Address const& addr, Proxy* p)
{
    if (servers_pool.empty()) {
        for (int i = 0; i < 8; ++i) {
            servers_pool.push_back(new Server);
            LOG(DEBUG) << "Allocate Server: " << servers_pool.back();
        }
    }
    Server* s = servers_pool.back();
    s->_reconnect(addr, p);
    servers_pool.pop_back();
    return s;
}

Server* Server::get_server(util::Address addr, Proxy* p)
{
    auto i = servers_map.find(addr);
    if (i == servers_map.end()) {
        Server* s = Server::_alloc_server(addr, p);
        servers_map.insert(std::make_pair(std::move(addr), s));
        return s;
    }
    return i->second;
}

void Server::close_server(Server* server)
{
    LOG(DEBUG) << "Close Server " << server << " (" << server->fd << ") at " << server->addr.host << ':' << server->addr.port;
    server->close();
    server->_buffer.clear();
    server->_commands.clear();
    server->_ready_commands.clear();
    server->attached_long_connections.clear();
    ::remove_entry(server);
    servers_pool.push_back(server);
}

static Buffer const READ_ONLY_CMD(Buffer::from_string("READONLY\r\n"));

void Server::send_readonly_for_each_conn()
{
    ::on_server_connected =
        [](int fd, std::vector<util::sref<DataCommand>>& cmds)
        {
            READ_ONLY_CMD.write(fd);
            cmds.push_back(util::sref<DataCommand>(nullptr));
        };
}
