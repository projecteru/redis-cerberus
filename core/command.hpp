#ifndef __CERBERUS_COMMAND_HPP__
#define __CERBERUS_COMMAND_HPP__

#include <set>
#include <vector>

#include "utils/pointer.h"
#include "buffer.hpp"

struct iovec;

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
    public:
        util::sref<Client> client;
        Buffer arr_payload;
        std::vector<util::sptr<Command>> commands;
        int awaiting_count;

        CommandGroup(CommandGroup const&) = delete;

        explicit CommandGroup(util::sref<Client> c)
            : client(c)
            , awaiting_count(0)
        {}

        virtual ~CommandGroup() {}

        void command_responsed();
        void append_command(util::sptr<Command> c);
        virtual void append_buffer_to(std::vector<struct iovec>& iov);
        virtual int total_buffer_size() const;
    };

    std::vector<util::sptr<CommandGroup>> split_client_command(
        Buffer& buffer, util::sref<Client> cli);

}

#endif /* __CERBERUS_COMMAND_HPP__ */
