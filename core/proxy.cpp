#include <cppformat/format.h>

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
    , _proxy_already_updated(false)
    , addr(std::move(a))
{
    LOG(DEBUG) << "Create " << this->str();
    fctl::set_nonblocking(fd);
    fctl::connect_fd(this->addr.host, this->addr.port, this->fd);
    p->poll_add_rw(this);
}

void SlotsMapUpdater::_await_data()
{
    this->_proxy->poll_ro(this);
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
    LOG(DEBUG) << "*Updated from " << this->str();
    this->_nodes = parse_slot_map(rsp[0]->get_buffer().to_string(), this->addr.host);

    for (RedisNode const& node: this->_nodes) {
        if (node.addr.host.empty()) {
            LOG(INFO) << fmt::format("Discard result of {} because address is empty string :{}",
                                     this->str(), node.addr.port);
            continue;
        }
        for (auto const& begin_end: node.slot_ranges) {
            for (slot s = begin_end.first; s <= begin_end.second; ++s) {
                this->_covered_slots.insert(s);
            }
        }
        this->_remotes.insert(node.addr);
    }

    this->_notify_updated();
}

void SlotsMapUpdater::on_events(int events)
{
    if (poll::event_is_hup(events)) {
        LOG(ERROR) << "Failed to retrieve slot map from " << this->str()
                   << ". Closed because remote hung up.";
        return this->_notify_updated();
    }
    if (poll::event_is_write(events)) {
        return this->_send_cmd();
    }
    if (poll::event_is_read(events)) {
        try {
            this->_recv_rsp();
        } catch (BadRedisMessage& e) {
            LOG(ERROR) << "Receive bad message from server on update from "
                       << this->str() << " because " << e.what();
            return this->_notify_updated();
        }
    }
}

void SlotsMapUpdater::_notify_updated()
{
    this->close();
    if (!this->_proxy_already_updated) {
        this->_proxy->notify_slot_map_updated(this->get_nodes(), this->_remotes,
                                              this->_covered_slots.size());
    }
}

std::string SlotsMapUpdater::str() const
{
    return fmt::format("SlotsMapUpdater({}@{})[{}]", this->fd,
                       static_cast<void const*>(this), this->addr.str());
}

Proxy::Proxy()
    : _clients_count(0)
    , _long_conns_count(0)
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

void Proxy::_set_slot_map(std::vector<RedisNode> const& map,
                          std::set<util::Address> const& remotes)
{
    _server_map.replace_map(map, this);
    _slot_map_expired = false;
    cerb_global::set_remotes(std::move(remotes));
    cerb_global::set_cluster_ok(true);
    LOG(INFO) << "Slot map updated";
    LOG(DEBUG) << "Retry MOVED or ASK: " << this->_retrying_commands.size();
    if (this->_retrying_commands.empty()) {
        return;
    }

    std::set<Server*> svrs;
    std::vector<util::sref<DataCommand>> retrying(std::move(this->_retrying_commands));
    for (util::sref<DataCommand> cmd: retrying) {
        Server* s = cmd->select_server(this);
        if (s == nullptr) {
            LOG(DEBUG) << "Select null server after slot map updated";
            _slot_map_expired = true;
            continue;
        }
        svrs.insert(s);
    }

    for (Server* svr: svrs) {
        this->poll_rw(svr);
    }
}

void Proxy::_update_slot_map_failed()
{
    for (util::sptr<SlotsMapUpdater> const& u: this->_slot_updaters) {
        if (!u->closed()) {
            return;
        }
    }
    LOG(DEBUG) << fmt::format("{} updaters all closed", this->_slot_updaters.size());

    if (!cerb_global::cluster_req_full_cov() && !this->_slot_updaters.empty()) {
        LOG(DEBUG) << fmt::format("Doesn't request full coverage, try {} updaters", this->_slot_updaters.size());
        util::sptr<SlotsMapUpdater> const& candidate_updater = *util::max_element(
            this->_slot_updaters,
            [](util::sptr<SlotsMapUpdater> const& u)
            {
                return u->covered_slots();
            });

        LOG(DEBUG) << fmt::format("Cluster contains {} slots", candidate_updater->covered_slots());
        if (candidate_updater->covered_slots() != 0) {
            this->_set_slot_map(candidate_updater->get_nodes(),
                                       candidate_updater->get_remotes());
            return this->_move_closed_slot_updaters();
        }
    }

    this->_move_closed_slot_updaters();
    cerb_global::set_cluster_ok(false);
    LOG(DEBUG) << "Failed to retrieve slot map, discard all commands.";
    _server_map.clear();
    std::vector<util::sref<DataCommand>> cmds(std::move(this->_retrying_commands));
    for (util::sref<DataCommand> c: cmds) {
        c->on_remote_responsed(Buffer("-CLUSTERDOWN The cluster is down\r\n"), true);
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
            LOG(INFO) << "Disconnect " << addr.str() << " for " << e.what();
        } catch (UnknownHost& e) {
            LOG(ERROR) << "Disconnect " << addr.str() << " for " << e.what();
        }
    };
    if (_slot_updaters.empty()) {
        this->_update_slot_map_failed();
    }
}

void Proxy::notify_slot_map_updated(std::vector<RedisNode> const& nodes,
                                    std::set<util::Address> const& remotes, msize_t covered_slots)
{
    if (covered_slots < CLUSTER_SLOT_COUNT) {
        LOG(INFO) << fmt::format("Discard result because only {} slots covered", covered_slots);
        return this->_update_slot_map_failed();
    }
    this->_set_slot_map(nodes, remotes);
    this->_move_closed_slot_updaters();
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

void Proxy::_move_closed_slot_updaters()
{
    /* shall not use
     *     _finished_slot_updaters = std::move(_slot_updaters)
     * because _finished_slot_updaters may still contains some closed connections
     */
    for (auto& u: this->_slot_updaters) {
        u->proxy_updated();
        this->_finished_slot_updaters.push_back(std::move(u));
    }
    this->_slot_updaters.clear();
}

void Proxy::retry_move_ask_command_later(util::sref<DataCommand> cmd)
{
    LOG(DEBUG) << "Retry later: " << cmd.id().str() << " for " << cmd->group->client->str();
    this->_retrying_commands.push_back(cmd);
}

void Proxy::inactivate_long_conn(Connection* conn)
{
    this->_inactive_long_connections.insert(conn);
}

static void poll_ctl(Proxy* p, std::map<Connection*, bool> conn_polls)
{
    LOG(DEBUG) << "*poll ctl " << conn_polls.size();
    for (std::pair<Connection*, bool> conn_writable: conn_polls) {
        Connection* c = conn_writable.first;
        if (c->closed()) {
            continue;
        }
        LOG(DEBUG) << " poll ctl " << c->str();
        if (conn_writable.second) {
            p->poll_rw(c);
        } else {
            p->poll_ro(c);
        }
    }
}

void Proxy::handle_events(poll::pevent events[], int nfds)
{
    LOG(DEBUG) << "*poll wait: " << nfds;
    std::set<Connection*> active_conns;
    for (Connection* c: this->_inactive_long_connections) {
        c->close();
    }
    std::set<Connection*> closed_conns(std::move(this->_inactive_long_connections));

    for (int i = 0; i < nfds; ++i) {
        Connection* conn = static_cast<Connection*>(events[i].data.ptr);
        LOG(DEBUG) << "*poll process " << conn->str();
        if (closed_conns.find(conn) != closed_conns.end()) {
            continue;
        }
        active_conns.insert(conn);
        try {
            conn->on_events(events[i].events);
        } catch (IOErrorBase& e) {
            LOG(ERROR) << "IOError: " << e.what() << " :: " << "Close " << conn->str();
            conn->on_error();
            closed_conns.insert(conn);
        }
    }
    LOG(DEBUG) << "*poll clean";

    ::poll_ctl(this, std::move(this->_conn_poll_type));
    for (Connection* c: active_conns) {
        c->after_events(active_conns);
    }
    this->_finished_slot_updaters.clear();
    if (this->_should_update_slot_map()) {
        LOG(DEBUG) << "Should update slot map";
        this->_retrieve_slot_map();
        /* do it again after try updating slot map
         * because some client may get CLUSTERDOWN message when no available remotes
         */
        ::poll_ctl(this, std::move(this->_conn_poll_type));
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
    LOG(DEBUG) << "Pop " << cli->str();
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

void Proxy::poll_add_ro(Connection* conn)
{
    if (poll::poll_add_read(this->epfd, conn->fd, conn)) {
        throw cerb::SystemError("poll r+" + conn->str(), errno);
    }
}

void Proxy::poll_add_rw(Connection* conn)
{
    if (poll::poll_add_write(this->epfd, conn->fd, conn)) {
        throw cerb::SystemError("poll rw+" + conn->str(), errno);
    }
}

void Proxy::poll_ro(Connection* conn)
{
    if (poll::poll_read(this->epfd, conn->fd, conn)) {
        throw cerb::SystemError("poll r*" + conn->str(), errno);
    }
}

void Proxy::poll_rw(Connection* conn)
{
    if (poll::poll_write(this->epfd, conn->fd, conn)) {
        throw cerb::SystemError("poll rw*" + conn->str(), errno);
    }
}

void Proxy::poll_del(Connection* conn)
{
    poll::poll_del(this->epfd, conn->fd);
}
