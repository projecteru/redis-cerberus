#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>
#include <map>
#include <atomic>

#include "command.hpp"
#include "slot_map.hpp"
#include "connection.hpp"
#include "acceptor.hpp"
#include "utils/pointer.h"
#include "syscalls/poll.h"

namespace cerb {

    class Proxy;
    class Server;

    class SlotsMapUpdater
        : public Connection
    {
        Proxy* _proxy;
        Buffer _rsp;
        std::vector<RedisNode> _nodes;
        std::set<util::Address> _remotes;
        std::set<slot> _covered_slots;
        bool _proxy_already_updated;

        void _send_cmd();
        void _recv_rsp();
        void _await_data();
        void _notify_updated();
    public:
        util::Address const addr;

        SlotsMapUpdater(util::Address addr, Proxy* p);

        void on_error()
        {
            this->_notify_updated();
        }

        void on_events(int events);
        std::string str() const;

        std::vector<RedisNode> const& get_nodes() const
        {
            return this->_nodes;
        }

        std::set<util::Address> const& get_remotes() const
        {
            return this->_remotes;
        }

        msize_t covered_slots() const
        {
            return this->_covered_slots.size();
        }

        void proxy_updated()
        {
            this->_proxy_already_updated = true;
        }
    };

    class Proxy {
        int _clients_count;
        int _long_conns_count;

        SlotMap _server_map;
        std::vector<util::sptr<SlotsMapUpdater>> _slot_updaters;
        std::vector<util::sptr<SlotsMapUpdater>> _finished_slot_updaters;
        std::vector<util::sref<DataCommand>> _retrying_commands;
        std::set<Connection*> _inactive_long_connections;
        Interval _total_cmd_elapse;
        Interval _total_remote_cost;
        long _total_cmd;
        Interval _last_cmd_elapse;
        Interval _last_remote_cost;
        bool _slot_map_expired;
        bool _fd_closed;
        std::map<Connection*, bool> _conn_poll_type;

        bool _should_update_slot_map() const;
        void _retrieve_slot_map();
        void _set_slot_map(std::vector<RedisNode> const& map,
                           std::set<util::Address> const& remotes);
        void _update_slot_map_failed();
        void _update_slot_map();
        void _move_closed_slot_updaters();
    public:
		static std::atomic<long> cmds_per_sec;
        int epfd;
        Acceptor acceptor;

        explicit Proxy(int listen_port);
        ~Proxy();

        Proxy(Proxy const&) = delete;

        void set_conn_poll_ro(Connection* conn)
        {
            _conn_poll_type[conn] = _conn_poll_type[conn] || false;
        }

        void set_conn_poll_rw(Connection* conn)
        {
            _conn_poll_type[conn] = true;
        }

        int clients_count() const
        {
            return _clients_count;
        }

        bool accepting() const
        {
            return this->acceptor.accepting();
        }

        void incr_long_conn()
        {
            ++this->_long_conns_count;
        }

        void decr_long_conn()
        {
            --this->_long_conns_count;
        }

        int long_conns_count() const
        {
            return this->_long_conns_count;
        }

        long total_cmd() const
        {
            return _total_cmd;
        }

        Interval total_cmd_elapse() const
        {
            return _total_cmd_elapse;
        }

        Interval total_remote_cost() const
        {
            return _total_remote_cost;
        }

        Interval last_cmd_elapse() const
        {
            return _last_cmd_elapse;
        }

        Interval last_remote_cost() const
        {
            return _last_remote_cost;
        }

        Server* random_addr()
        {
            return _server_map.random_addr();
        }

        Server* get_server_by_slot(slot key_slot);
        void notify_slot_map_updated(std::vector<RedisNode> const& nodes,
                                     std::set<util::Address> const& remotes,
                                     msize_t covered_slots);
        void update_slot_map();
        void retry_move_ask_command_later(util::sref<DataCommand> cmd);
        void inactivate_long_conn(Connection* conn);
        void handle_events(poll::pevent events[], int nfds);
        void new_client(int client_fd);
        void pop_client(Client* cli);
        void stat_proccessed(Interval cmd_elapse, Interval remote_cost);

        void poll_add_ro(Connection* conn);
        void poll_add_rw(Connection* conn);
        void poll_ro(Connection* conn);
        void poll_rw(Connection* conn);
        void poll_del(Connection* conn);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
