#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

#include "command.hpp"
#include "slot_map.hpp"
#include "connection.hpp"
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

        void _send_cmd();
        void _recv_rsp();
        void _await_data();
    public:
        util::Address const addr;

        SlotsMapUpdater(util::Address addr, Proxy* p);

        void on_events(int events);
        void on_error();
    };

    class Proxy {
        int _clients_count;

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

        bool _should_update_slot_map() const;
        void _retrieve_slot_map();
        void _set_slot_map(std::vector<RedisNode> map, std::set<util::Address> remotes);
        void _update_slot_map_failed();
        void _update_slot_map();
    public:
        int epfd;

        Proxy();
        ~Proxy();

        Proxy(Proxy const&) = delete;

        int clients_count() const
        {
            return _clients_count;
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
        void notify_slot_map_updated(std::vector<RedisNode> nodes);
        void update_slot_map();
        void retry_move_ask_command_later(util::sref<DataCommand> cmd);
        void inactivate_long_conn(Connection* conn);
        void handle_events(poll::pevent events[], int nfds);
        void new_client(int client_fd);
        void pop_client(Client* cli);
        void stat_proccessed(Interval cmd_elapse, Interval remote_cost);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
