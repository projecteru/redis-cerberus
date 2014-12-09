#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

#include "utils/pointer.h"
#include "common.hpp"
#include "command.hpp"
#include "slot_map.hpp"
#include "fdutil.hpp"

namespace cerb {

    class Proxy;

    class Connection
        : public FDWrapper
    {
    public:
        explicit Connection(int fd)
            : FDWrapper(fd)
        {}

        virtual ~Connection() {}

        virtual void triggered(Proxy* p, int events) = 0;
    };

    class Acceptor
        : public Connection
    {
    public:
        explicit Acceptor(int fd)
            : Connection(fd)
        {}

        void triggered(Proxy* p, int events);
    };

    class Server
        : public Connection
    {
        Proxy* const _proxy;
        Buffer _buffer;

        std::vector<util::sref<Command>> _commands;
        std::vector<util::sref<Command>> _ready_commands;

        void _send_to();
        void _recv_from();
    public:
        Server(std::string const& host, int port, Proxy* p);
        ~Server();

        void triggered(Proxy* p, int events);

        void push_client_command(util::sref<Command> cmd);
        void pop_client(Client* cli);
    };

    class Client
        : public Connection
    {
        void _send_to();
        void _recv_from();

        Proxy* const _proxy;
        std::set<Server*> _peers;
        std::vector<util::sptr<CommandGroup>> _awaiting_groups;
        std::vector<util::sptr<CommandGroup>> _ready_groups;
        int _awaiting_count;
        Buffer _buffer;

        void _process();
        void _response_ready();
    public:
        Client(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
            , _awaiting_count(0)
        {}

        ~Client();

        void triggered(Proxy* p, int events);
        void group_responsed();
        void add_peer(Server* svr);
    };

    class SlotsMapUpdater
        : public Connection
    {
        Proxy* _proxy;
        std::map<slot, util::Address> _updated_map;

        void _send_cmd();
        void _recv_rsp();
    public:
        SlotsMapUpdater(util::Address const& addr, Proxy* p);

        void triggered(Proxy* p, int events);

        bool success() const
        {
            return !_updated_map.empty();
        }

        std::map<slot, util::Address> deliver_map()
        {
            return std::move(_updated_map);
        }
    };

    class Proxy {
        SlotMap<Server> _server_map;
        std::vector<util::sptr<SlotsMapUpdater>> _slot_updaters;
        int _active_slot_updaters_count;
        std::vector<util::sref<Command>> _move_ask_command;

        bool _should_update_slot_map() const;
        void _retrieve_slot_map();
        void _set_slot_map(std::map<slot, util::Address> map);
        void _update_slot_map();
        void _loop();
    public:
        int epfd;

        explicit Proxy(util::Address const& remote);
        ~Proxy();

        Proxy(Proxy const&) = delete;

        Server* get_server_by_slot(slot key_slot)
        {
            return _server_map.get_by_slot(key_slot);
        }

        void notify_slot_map_updated();
        void retry_move_ask_command_later(util::sref<Command> cmd);
        void run(int listen_port);
        void accept_from(int listen_fd);
        void shut_server(Server* svr);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
