#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <mutex>
#include <vector>

#include "utils/pointer.h"
#include "command.hpp"
#include "slot_map.hpp"
#include "connection.hpp"

namespace cerb {

    class Proxy;
    class Server;

    class Acceptor
        : public Connection
    {
        Proxy* const _proxy;
    public:
        Acceptor(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
        {}

        void on_events(int events);
        void on_error();
    };

    class SlotsMapUpdater
        : public Connection
    {
        Proxy* _proxy;
        std::vector<RedisNode> _updated_map;
        Buffer _rsp;

        void _send_cmd();
        void _recv_rsp();
        void _await_data();
    public:
        util::Address const addr;

        SlotsMapUpdater(util::Address addr, Proxy* p);

        void on_events(int events);
        void on_error();

        bool success() const
        {
            return !_updated_map.empty();
        }

        std::vector<RedisNode> deliver_map()
        {
            return std::move(_updated_map);
        }
    };

    class Proxy {
        int _clients_count;

        SlotMap _server_map;
        std::set<util::Address> _candidate_addrs;
        std::mutex _candidate_addrs_mutex;
        std::vector<util::sptr<SlotsMapUpdater>> _slot_updaters;
        std::vector<util::sptr<SlotsMapUpdater>> _finished_slot_updaters;
        int _active_slot_updaters_count;
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
        void _close_servers(std::set<Server*> servers);
        void _set_slot_map(std::vector<RedisNode> map);
        void _update_slot_map_failed(std::set<util::Address> addrs);
        void _update_slot_map();
        void _loop();
    public:
        int epfd;

        explicit Proxy(util::Address const& remote);
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
        void notify_slot_map_updated();
        void update_slot_map();
        void update_remotes(std::set<util::Address> remotes);
        void retry_move_ask_command_later(util::sref<DataCommand> cmd);
        void run(int listen_port);
        void accept_from(int listen_fd);
        void pop_client(Client* cli);
        void stat_proccessed(Interval cmd_elapse, Interval remote_cost);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
