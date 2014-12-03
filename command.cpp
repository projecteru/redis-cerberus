#include "message.hpp"
#include "command.hpp"
#include "exceptions.hpp"
#include "proxy.hpp"

#include <cstdio>

using namespace cerb;

namespace {

    class ClientCommandSplitter
        : public cerb::msg::MessageSplitterBase<
            Buffer::iterator, ClientCommandSplitter>
    {
        typedef cerb::msg::MessageSplitterBase<
            Buffer::iterator, ClientCommandSplitter> BaseType;
    public:
        Buffer::iterator last_command_begin;

        std::string last_command;
        std::vector<std::vector<byte>> last_keys;

        std::vector<util::sptr<Command>> splitted_commands;
        std::function<void(byte)> on_byte;
        std::function<void(Buffer::iterator)> on_element;

        util::sref<Client> client;

        ClientCommandSplitter(Buffer::iterator i, util::sref<Client> cli)
            : BaseType(i)
            , last_command_begin(i)
            , on_byte(std::bind(&ClientCommandSplitter::on_command_byte,
                                this, std::placeholders::_1))
            , on_element(std::bind(&ClientCommandSplitter::on_raw_element,
                                   this, std::placeholders::_1))
            , client(cli)
        {}

        ClientCommandSplitter(ClientCommandSplitter&& rhs)
            : BaseType(std::move(rhs))
            , last_command_begin(rhs.last_command_begin)
            , last_command(std::move(rhs.last_command))
            , splitted_commands(std::move(rhs.splitted_commands))
            , on_byte(std::move(rhs.on_byte))
            , on_element(std::move(rhs.on_element))
            , client(rhs.client)
        {}

        void on_raw_element(Buffer::iterator i)
        {
            splitted_commands.push_back(util::mkptr(new Command(
                std::move(last_command), last_command_begin, i, client)));
            last_command_begin = i;
            BaseType::on_element(i);
        }

        void on_command_arr_first_element(Buffer::iterator it)
        {
            // printf(" # Command %s\n", last_command.c_str());
            BaseType::on_element(it);
            this->on_byte =
                [&](byte b)
                {
                    this->on_key_byte(b);
                };
            this->on_element =
                [&](Buffer::iterator i)
                {
                    this->on_command_arr_after_first_element(i);
                };
        }

        void on_command_arr_after_first_element(Buffer::iterator it)
        {
            BaseType::on_element(it);
            this->on_byte = [&](byte) {};
            this->on_element =
                [&](Buffer::iterator i)
                {
                    BaseType::on_element(i);
                };
        }

        void on_command_byte(byte b)
        {
            // printf(" Command byte %d %c\n", b, b);
            last_command += std::toupper(b);
        }

        void on_key_byte(byte b)
        {
            // printf(" Key byte %d %c\n", b, b);
            if (last_keys.empty()) {
                last_keys.push_back(std::vector<byte>());
            }
            last_keys.back().push_back(b);
        }

        void on_arr_end(Buffer::iterator i)
        {
            BaseType::on_element(i);
            this->on_byte = std::bind(&ClientCommandSplitter::on_command_byte,
                                      this, std::placeholders::_1);
            this->on_element = std::bind(&ClientCommandSplitter::on_raw_element,
                                         this, std::placeholders::_1);
            splitted_commands.push_back(util::mkptr(new Command(
                std::move(last_command), std::move(last_keys),
                last_command_begin, i, client, true)));
            last_command_begin = i;
        }

        void on_arr(cerb::rint size, Buffer::iterator i)
        {
            if (!_nested_array_element_count.empty()) {
                throw BadRedisMessage("Invalid nested array as client command");
            }
            BaseType::on_arr(size, i);
            if (size == 0) {
                return;
            }
            this->on_byte = std::bind(&ClientCommandSplitter::on_command_byte,
                                      this, std::placeholders::_1);
            this->on_element = std::bind(
                &ClientCommandSplitter::on_command_arr_first_element,
                this, std::placeholders::_1);
        }

        typedef std::vector<util::sptr<Command>>::iterator iterator;

        iterator begin()
        {
            return splitted_commands.begin();
        }

        iterator end()
        {
            return splitted_commands.end();
        }
    };

}

void Command::copy_response(Buffer::iterator begin, Buffer::iterator end)
{
    this->buffer.copy_from(begin, end);
    this->client->command_responsed();
}

std::vector<util::sptr<Command>> cerb::split_client_command(
    Buffer& buffer, util::sref<Client> cli)
{
    ClientCommandSplitter c(cerb::msg::split_by(
        buffer.begin(), buffer.end(), ClientCommandSplitter(
            buffer.begin(), cli)));
    return std::move(c.splitted_commands);
}
