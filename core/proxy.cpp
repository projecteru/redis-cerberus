#include "proxy.hpp"
#include "server.hpp"
#include "client.hpp"
#include "response.hpp"
#include "globals.hpp"
#include "except/exceptions.hpp"
#include "utils/string.h"
#include "utils/alg.hpp"
#include "utils/logging.hpp"
#include "syscalls/poll.h"
#include "syscalls/cio.h"
#include "syscalls/fctl.h"

using namespace cerb;

SlotsMapUpdater::SlotsMapUpdater(util::Address a, Proxy* p)
    : Connection(fctl::new_stream_socket())
    , _proxy(p)
    , addr(std::move(a))
{
    LOG(DEBUG) << "*Create updater " << this->fd << " in " << this;
    fctl::set_nonblocking(fd);
    fctl::connect_fd(this->addr.host, this->addr.port, this->fd);
    poll::poll_add(_proxy->epfd, this->fd, this);
}

void SlotsMapUpdater::_await_data()
{
    poll::poll_read(_proxy->epfd, this->fd, this);
}

void SlotsMapUpdater::_send_cmd()
{
    write_slot_map_cmd_to(this->fd);
    this->_await_data();
}

void SlotsMapUpdater::_recv_rsp()
{
    _rsp.read(this->fd);
    std::vector<util::sptr<Response>> rsp(split_server_response(_rsp));
    if (rsp.size() == 0) {
        return this->_await_data();
    }
    if (rsp.size() != 1) {
        throw BadRedisMessage("Ask cluster nodes returns responses with size=" +
                              util::str(int(rsp.size())));
    }
    LOG(DEBUG) << "*Updated from " << this->fd << " in " << this;
    this->close();
    _proxy->notify_slot_map_updated(
        parse_slot_map(rsp[0]->dump_buffer().to_string(), this->addr.host));
}

void SlotsMapUpdater::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        LOG(ERROR) << "Failed to retrieve slot map from " << this->addr.str()
                   << ". Closed because remote hung up.";
        this->close();
        return _proxy->notify_slot_map_updated({});
    }
    if (poll::event_is_write(events)) {
        return this->_send_cmd();
    }
    if (poll::event_is_read(events)) {
        try {
            this->_recv_rsp();
        } catch (BadRedisMessage& e) {
            LOG(ERROR) << "Receive bad message from server on update from "
                       << this->fd
                       << " because: " << e.what();
            this->close();
            return _proxy->notify_slot_map_updated({});
        }
    }
}

void SlotsMapUpdater::on_error()
{
    this->close();
    _proxy->notify_slot_map_updated({});
}

Proxy::Proxy()
    : _clients_count(0)
    , _total_cmd_elapse(0)
    , _total_remote_cost(0)
    , _total_cmd(0)
    , _last_cmd_elapse(0)
    , _last_remote_cost(0)
    , _slot_map_expired(true)
    , epfd(poll::poll_create())
{}

Proxy::~Proxy()
{
    cio::close(epfd);
}

void Proxy::_set_slot_map(std::vector<RedisNode> map, std::set<util::Address> remotes)
{
    _server_map.replace_map(map, this);
    _slot_map_expired = false;
    cerb_global::set_remotes(std::move(remotes));
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

    for (Server* svr: svrs) {
        poll::poll_write(this->epfd, svr->fd, svr);
    }
}

void Proxy::_update_slot_map_failed()
{
    for (util::sptr<SlotsMapUpdater> const& u: this->_slot_updaters) {
        if (!u->closed()) {
            return;
        }
    }
    this->_finished_slot_updaters = std::move(this->_slot_updaters);
    LOG(DEBUG) << "Failed to retrieve slot map, discard all commands.";
    _server_map.clear();
    std::vector<util::sref<DataCommand>> cmds(std::move(this->_retrying_commands));
    for (util::sref<DataCommand> c: cmds) {
        c->on_remote_responsed(Buffer::from_string("-CLUSTERDOWN The cluster is down\r\n"), true);
    }
    _slot_map_expired = false;
}

void Proxy::_retrieve_slot_map()
{
    std::set<util::Address> remotes(cerb_global::get_remotes());
    if (remotes.empty()) {
        LOG(ERROR) << "No remotes set";
        return this->_update_slot_map_failed();
    }
    for (util::Address const& addr: remotes) {
        try {
            this->_slot_updaters.push_back(
                util::mkptr(new SlotsMapUpdater(addr, this)));
        } catch (ConnectionRefused& e) {
            LOG(INFO) << e.what();
            LOG(INFO) << "Disconnected: " << addr.str();
        } catch (UnknownHost& e) {
            LOG(ERROR) << e.what();
        }
    };
    if (_slot_updaters.empty()) {
        this->_update_slot_map_failed();
    }
}

void Proxy::notify_slot_map_updated(std::vector<RedisNode> nodes)
{
    std::set<util::Address> remotes;
    std::set<slot> covered_slots;
    for (RedisNode const& node: nodes) {
        if (node.addr.host.empty()) {
            LOG(DEBUG) << "Discard result because address is empty string :" << node.addr.port;
            return this->_update_slot_map_failed();
        }
        for (auto const& begin_end: node.slot_ranges) {
            for (slot s = begin_end.first; s <= begin_end.second; ++s) {
                covered_slots.insert(s);
            }
        }
        remotes.insert(node.addr);
    }
    if (covered_slots.size() < CLUSTER_SLOT_COUNT) {
        LOG(DEBUG) << "Discard result because only " << covered_slots.size() << " slots covered";
        return this->_update_slot_map_failed();
    }
    this->_set_slot_map(std::move(nodes), std::move(remotes));
    for (auto& u: this->_slot_updaters) {
        this->_finished_slot_updaters.push_back(std::move(u));
    }
    this->_slot_updaters.clear();
}

void Proxy::update_slot_map()
{
    _slot_map_expired = true;
}

bool Proxy::_should_update_slot_map() const
{
    return this->_slot_updaters.empty() &&
        (!this->_retrying_commands.empty() || this->_slot_map_expired);
}

void Proxy::retry_move_ask_command_later(util::sref<DataCommand> cmd)
{
    LOG(DEBUG) << "Retry later: " << cmd.id().str() << " for client " << cmd->group->client->fd;
    this->_retrying_commands.push_back(cmd);
}

void Proxy::inactivate_long_conn(Connection* conn)
{
    this->_inactive_long_connections.insert(conn);
}

void Proxy::handle_events(poll::pevent events[], int nfds)
{
    std::set<Connection*> active_conns;
    for (Connection* c: this->_inactive_long_connections) {
        c->close();
    }
    std::set<Connection*> closed_conns(std::move(this->_inactive_long_connections));

    LOG(DEBUG) << "*poll wait: " << nfds;
    for (int i = 0; i < nfds; ++i) {
        Connection* conn = static_cast<Connection*>(events[i].data.ptr);
        LOG(DEBUG) << "*poll process " << conn->fd << " in " << conn;
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
    LOG(DEBUG) << "*poll clean";
    for (Connection* c: active_conns) {
        c->after_events(active_conns);
    }
    this->_finished_slot_updaters.clear();
    if (this->_should_update_slot_map()) {
        LOG(DEBUG) << "Should update slot map";
        this->_retrieve_slot_map();
    }
    LOG(DEBUG) << "*poll done";
}

Server* Proxy::get_server_by_slot(slot key_slot)
{
    Server* s = _server_map.get_by_slot(key_slot);
    return (s == nullptr || s->closed()) ? nullptr : s;
}

void Proxy::new_client(int client_fd)
{
    new Client(client_fd, this);
    ++this->_clients_count;
}

void Proxy::pop_client(Client* cli)
{
    LOG(DEBUG) << "Pop client " << cli->fd;
    util::erase_if(
        this->_retrying_commands,
        [cli](util::sref<DataCommand> cmd)
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
