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

        virtual ~Command() = default;

        virtual Server* select_server(Proxy* proxy) = 0;
        virtual void on_remote_responsed(Buffer rsp, bool error);

        void responsed();

        Command(Buffer b, util::sref<CommandGroup> g)
            : buffer(std::move(b))
            , group(g)
        {}

        explicit Command(util::sref<CommandGroup> g)
            : group(g)
        {}

        Command(Command const&) = delete;

        static void allow_write_commands();
    };

    class DataCommand
        : public Command
    {
    public:
        DataCommand(Buffer b, util::sref<CommandGroup> g)
            : Command(std::move(b), g)
        {}

        explicit DataCommand(util::sref<CommandGroup> g)
            : Command(g)
        {}

        Time sent_time;
        Time resp_time;

        Interval remote_cost() const
        {
            return resp_time - sent_time;
        }
    };

    class CommandGroup {
    public:
        util::sref<Client> const client;

        explicit CommandGroup(util::sref<Client> cli)
            : client(cli)
        {}

        CommandGroup(CommandGroup const&) = delete;
        virtual ~CommandGroup() = default;

        virtual bool long_connection() const
        {
            return false;
        }

        virtual void deliver_client(Proxy*) {}
        virtual bool wait_remote() const = 0;
        virtual void select_remote(Proxy* proxy) = 0;
        virtual void append_buffer_to(BufferSet& b) = 0;
        virtual int total_buffer_size() const = 0;
        virtual void command_responsed() = 0;
        virtual void collect_stats(Proxy*) const {}
    };

    void split_client_command(Buffer& buffer, util::sref<Client> cli);

}

#endif /* __CERBERUS_COMMAND_HPP__ */
