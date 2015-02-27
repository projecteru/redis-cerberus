#ifndef __CERBERUS_COMMAND_HPP__
#define __CERBERUS_COMMAND_HPP__

#include <set>
#include <vector>

#include "utils/pointer.h"
#include "buffer.hpp"

namespace cerb {

    class Proxy;
    class Client;
    class Server;
    class CommandGroup;

    class Command {
    public:
        Buffer buffer;
        util::sref<CommandGroup> const group;
        bool const need_send;

        virtual ~Command() {}

        virtual Server* select_server(Proxy* proxy) = 0;
        virtual void copy_response(Buffer rsp, bool error);

        Command(Buffer b, util::sref<CommandGroup> g, bool s)
            : buffer(std::move(b))
            , group(g)
            , need_send(s)
        {}

        Command(util::sref<CommandGroup> g, bool s)
            : group(g)
            , need_send(s)
        {}

        Command(Command const&) = delete;
    };

    class CommandGroup {
    protected:
        CommandGroup()
            : client(nullptr)
            , awaiting_count(0)
            , creation(Clock::now())
            , long_conn_command(true)
        {}
    public:
        util::sref<Client> client;
        Buffer arr_payload;
        std::vector<util::sptr<Command>> commands;
        int awaiting_count;
        Time const creation;
        bool const long_conn_command;

        CommandGroup(CommandGroup const&) = delete;

        explicit CommandGroup(util::sref<Client> c)
            : client(c)
            , awaiting_count(0)
            , creation(Clock::now())
            , long_conn_command(false)
        {}

        virtual ~CommandGroup();

        void command_responsed();
        void append_command(util::sptr<Command> c);
        virtual void append_buffer_to(std::vector<util::sref<Buffer>>& b);
        virtual int total_buffer_size() const;
        virtual void deliver_client(Proxy*, Client*) {}
    };

    void split_client_command(Buffer& buffer, util::sref<Client> cli);

}

#endif /* __CERBERUS_COMMAND_HPP__ */
