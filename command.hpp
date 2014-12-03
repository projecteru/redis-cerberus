#ifndef __CERBERUS_COMMAND_HPP__
#define __CERBERUS_COMMAND_HPP__

#include <vector>

#include "utils/pointer.h"
#include "typetraits.hpp"
#include "buffer.hpp"

namespace cerb {

    class Client;

    class Command {
    public:
        std::string command;
        std::vector<std::vector<byte>> keys;
        Buffer buffer;
        util::sref<Client> client;
        bool need_send;

        Command(std::string command, Buffer::iterator begin,
                Buffer::iterator end, util::sref<Client> cli)
            : command(std::move(command))
            , buffer(begin, end)
            , client(cli)
            , need_send(false)
        {}

        Command(std::string command, std::vector<std::vector<byte>> keys,
                Buffer::iterator begin, Buffer::iterator end,
                util::sref<Client> cli, bool need_send)
            : command(std::move(command))
            , keys(std::move(keys))
            , buffer(begin, end)
            , client(cli)
            , need_send(need_send)
        {}

        Command(Command const&) = delete;

        void copy_response(Buffer::iterator begin, Buffer::iterator end);
    };

    std::vector<util::sptr<Command>> split_client_command(
        Buffer& buffer, util::sref<Client> cli);

}

#endif /* __CERBERUS_COMMAND_HPP__ */
