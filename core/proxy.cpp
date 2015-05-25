#include <unistd.h>
#include <netdb.h>
#include <sys/epoll.h>

#include "proxy.hpp"
#include "server.hpp"
#include "client.hpp"
#include "response.hpp"
#include "exceptions.hpp"
#include "utils/string.h"
#include "utils/alg.hpp"
#include "utils/logging.hpp"

using namespace cerb;

static int const MAX_EVENTS = 1024;

void Acceptor::on_events(int)
{
    _proxy->accept_from(this->fd);
}

void Acceptor::on_error()
{
    LOG(ERROR) << "Accept error.";
}

SlotsMapUpdater::SlotsMapUpdater(util::Address a, Proxy* p)
    : Connection(new_stream_socket())
    , _proxy(p)
    , addr(std::move(a))
{
    set_nonblocking(fd);
    connect_fd(this->addr.host, this->addr.port, this->fd);
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
        parse_slot_map(rsp[0]->dump_buffer().to_string(), this->addr.host));
    _proxy->notify_slot_map_updated();
}

void SlotsMapUpdater::on_events(int events)
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
            LOG(ERROR) << "Receive bad message from server on update from "
                       << this->fd
                       << " because: " << e.what()
                       << " buffer length=" << this->_rsp.size();
            LOG(DEBUG) << "Dump buffer (before close): "
                       << this->_rsp.to_string();
            _proxy->notify_slot_map_updated();
        }
    }
    if (events & EPOLLOUT) {
        this->_send_cmd();
    }
}

void SlotsMapUpdater::on_error()
{
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
    _proxy->notify_slot_map_updated();
}

Proxy::Proxy(util::Address const& remote)
    : _clients_count(0)
    , _active_slot_updaters_count(0)
    , _total_cmd_elapse(0)
    , _total_remote_cost(0)
    , _total_cmd(0)
    , _last_cmd_elapse(0)
    , _last_remote_cost(0)
    , _slot_map_expired(true)
    , epfd(epoll_create(MAX_EVENTS))
{
    if (epfd == -1) {
        throw SystemError("epoll_create", errno);
    }
    std::lock_guard<std::mutex> _(this->_candidate_addrs_mutex);
    this->_candidate_addrs.insert(remote);
}

Proxy::~Proxy()
{
    ::close(epfd);
}

void Proxy::_update_slot_map()
{
    bool update_failed = _slot_updaters.end() == std::find_if(
        _slot_updaters.begin(), _slot_updaters.end(),
        [this](util::sptr<SlotsMapUpdater>& updater)
        {
            if (!updater->success()) {
                LOG(DEBUG) << "Discard a failed node";
                return false;
            }
            std::vector<RedisNode> nodes(updater->deliver_map());
            std::set<slot> covered_slots;
            for (RedisNode const& node: nodes) {
                if (node.addr.host.empty()) {
                    LOG(DEBUG) << "Discard result because address is empty string :" << node.addr.port;
                    return false;
                }
                for (auto const& begin_end: node.slot_ranges) {
                    for (slot s = begin_end.first; s <= begin_end.second; ++s) {
                        covered_slots.insert(s);
                    }
                }
            }
            if (covered_slots.size() < CLUSTER_SLOT_COUNT) {
                LOG(DEBUG) << "Discard result because only " << covered_slots.size() << " slots covered by "
                           << updater->addr.host << ':' << updater->addr.port;
                return false;
            }
            this->_set_slot_map(std::move(nodes));
            LOG(INFO) << "Slot map updated";
            return true;
        });
    this->_finished_slot_updaters = std::move(this->_slot_updaters);
    if (update_failed) {
        std::set<util::Address> r;
        for (util::sptr<SlotsMapUpdater> const& u: this->_finished_slot_updaters) {
            r.insert(u->addr);
        }
        this->_update_slot_map_failed(std::move(r));
    }
}

void Proxy::_close_servers(std::set<Server*> servers)
{
    for (Server* s: servers) {
        LOG(DEBUG) << "Closing server " << s;
        std::vector<util::sref<DataCommand>> c(s->deliver_commands());
        this->_retrying_commands.insert(_retrying_commands.end(), c.begin(), c.end());
        this->_inactive_long_connections.insert(
            s->attached_long_connections.begin(), s->attached_long_connections.end());
        Server::close_server(s);
    }
}

void Proxy::_set_slot_map(std::vector<RedisNode> map)
{
    this->_close_servers(_server_map.replace_map(map, this));

    _slot_map_expired = false;

    {
        std::lock_guard<std::mutex> _(this->_candidate_addrs_mutex);
        this->_candidate_addrs.clear();
    }

    if (this->_retrying_commands.empty()) {
        return;
    }

    LOG(DEBUG) << "Retry MOVED or ASK: " << this->_retrying_commands.size();
    std::set<Server*> svrs;
    std::vector<util::sref<DataCommand>> retrying(std::move(this->_retrying_commands));
    for (util::sref<DataCommand> cmd: retrying) {
        Server* s = cmd->select_server(this);
        if (s == nullptr) {
            LOG(ERROR) << "Select null server after slot map updated";
            _slot_map_expired = true;
            _retrying_commands.push_back(cmd);
            continue;
        }
        svrs.insert(s);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    for (Server* svr: svrs) {
        ev.data.ptr = svr;
        if (epoll_ctl(this->epfd, EPOLL_CTL_MOD, svr->fd, &ev) == -1) {
            throw SystemError("epoll_ctl+modio Proxy::_set_slot_map", errno);
        }
    }
}

void Proxy::_update_slot_map_failed(std::set<util::Address> addrs)
{
    LOG(DEBUG) << "Failed to retrieve slot map, discard all commands.";
    this->_close_servers(_server_map.deliver());
    for (Connection* c: this->_inactive_long_connections) {
        c->close();
    }
    std::vector<util::sref<DataCommand>> cmds(std::move(this->_retrying_commands));
    for (util::sref<DataCommand> c: cmds) {
        c->on_remote_responsed(Buffer::from_string("-CLUSTERDOWN The cluster is down\r\n"), true);
    }
    _slot_map_expired = false;
    std::lock_guard<std::mutex> _(this->_candidate_addrs_mutex);
    this->_candidate_addrs = std::move(addrs);
}

void Proxy::_retrieve_slot_map()
{
    std::set<util::Address> addrs;
    if (this->_candidate_addrs.empty()) {
        std::for_each(
            Server::addr_begin(), Server::addr_end(),
            [&](std::pair<util::Address, Server*> const& addr_server)
            {
                addrs.insert(addr_server.first);
            });
    } else {
        std::lock_guard<std::mutex> _(this->_candidate_addrs_mutex);
        addrs = std::move(this->_candidate_addrs);
    }
    for (util::Address const& addr: addrs) {
        try {
            this->_slot_updaters.push_back(
                util::mkptr(new SlotsMapUpdater(addr, this)));
            ++this->_active_slot_updaters_count;
        } catch (ConnectionRefused& e) {
            LOG(INFO) << e.what();
            LOG(INFO) << "Disconnected: " << addr.host << ':' << addr.port;
        } catch (UnknownHost& e) {
            LOG(ERROR) << e.what();
        }
    };
    if (_slot_updaters.empty()) {
        this->_update_slot_map_failed(std::move(addrs));
    }
}

void Proxy::notify_slot_map_updated()
{
    if (--_active_slot_updaters_count == 0) {
        _update_slot_map();
    }
}

void Proxy::update_slot_map()
{
    _slot_map_expired = true;
}

void Proxy::update_remotes(std::set<util::Address> remotes)
{
    std::lock_guard<std::mutex> _(this->_candidate_addrs_mutex);
    this->_candidate_addrs = std::move(remotes);
    _slot_map_expired = true;
}

bool Proxy::_should_update_slot_map() const
{
    return this->_slot_updaters.empty() &&
        (!this->_retrying_commands.empty() || this->_slot_map_expired);
}

void Proxy::retry_move_ask_command_later(util::sref<DataCommand> cmd)
{
    LOG(DEBUG) << "Retry later: " << cmd.id().str();
    this->_retrying_commands.push_back(cmd);
    LOG(DEBUG) << "A MOVED or ASK added for later retry - " << this->_retrying_commands.size();
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

    for (Connection* c: this->_inactive_long_connections) {
        c->close();
    }
    std::set<Connection*> closed_conns(std::move(this->_inactive_long_connections));

    LOG(DEBUG) << "*epoll wait: " << nfds;
    for (int i = 0; i < nfds; ++i) {
        Connection* conn = static_cast<Connection*>(events[i].data.ptr);
        LOG(DEBUG) << "*epoll process " << conn->fd;
        if (closed_conns.find(conn) != closed_conns.end()) {
            continue;
        }
        active_conns.insert(conn);
        try {
            conn->on_events(events[i].events);
        } catch (IOErrorBase& e) {
            LOG(ERROR) << "IOError: " << e.what() << " :: "
                       << "Close connection to " << conn->fd << " in " << conn;
            conn->on_error();
            closed_conns.insert(conn);
        }
    }
    LOG(DEBUG) << "*epoll done";
    for (Connection* c: active_conns) {
        c->after_events(active_conns);
    }
    _finished_slot_updaters.clear();
    if (this->_should_update_slot_map()) {
        LOG(DEBUG) << "Should update slot map";
        this->_retrieve_slot_map();
    }
}

Server* Proxy::get_server_by_slot(slot key_slot)
{
    Server* s = _server_map.get_by_slot(key_slot);
    return (s == nullptr || s->closed()) ? nullptr : s;
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
    util::erase_if(
        this->_retrying_commands,
        [&](util::sref<DataCommand> cmd)
        {
            return cmd->group->client.is(cli);
        });
    --this->_clients_count;
}

void Proxy::stat_proccessed(Interval cmd_elapse, Interval remote_cost)
{
    _total_cmd_elapse += cmd_elapse;
    ++_total_cmd;
    _last_cmd_elapse = cmd_elapse;
    _total_remote_cost += remote_cost;
    _last_remote_cost = remote_cost;
}
