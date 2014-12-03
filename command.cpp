#include <map>

#include "message.hpp"
#include "command.hpp"
#include "exceptions.hpp"
#include "proxy.hpp"

#include <cstdio>

using namespace cerb;

namespace {

    class DirectResponseCommand
        : public Command
    {
    public:
        DirectResponseCommand(std::string const& r, util::sref<Client> client)
            : Command(Buffer::from_string(r), client, false)
        {}

        DirectResponseCommand(char const* r, util::sref<Client> client)
            : Command(Buffer::from_string(r), client, false)
        {}

        DirectResponseCommand(Buffer b, util::sref<Client> client)
            : Command(std::move(b), client, false)
        {}
    };

    std::map<std::string, std::string> const QUICK_RSP({
        {"PING", "+PONG\r\n"},
    });

    util::sptr<Command> quick_rsp(std::string const& command,
                                  util::sref<Client> c)
    {
        auto fi = QUICK_RSP.find(command);
        if (fi == QUICK_RSP.end()) {
            return util::mkptr(new DirectResponseCommand(
                "-ERR unknown command '" + command + "'\r\n", c));
        }
        return util::mkptr(new DirectResponseCommand(fi->second, c));
    }

    class SpecialCommandParser {
    public:
        virtual void on_byte(byte b) = 0;
        virtual void on_element(Buffer::iterator i) = 0;
        virtual util::sptr<Command> spawn_command(
            util::sref<Client> c, Buffer::iterator end) = 0;
        virtual ~SpecialCommandParser() {}

        SpecialCommandParser() {}
        SpecialCommandParser(SpecialCommandParser const&) = delete;
    };

    class ForbiddenCommandParser
        : public SpecialCommandParser
    {
    public:
        ForbiddenCommandParser() {}

        void on_byte(byte) {}

        void on_element(Buffer::iterator) {}

        util::sptr<Command> spawn_command(util::sref<Client> c, Buffer::iterator)
        {
            return util::mkptr(new DirectResponseCommand(
                "-ERR this command is forbidden in cluster\r\n", c));
        }
    };

    class PingCommandParser
        : public SpecialCommandParser
    {
        Buffer::iterator begin;
        Buffer::iterator end;
        bool bad;
    public:
        PingCommandParser(Buffer::iterator arg_begin)
            : begin(arg_begin)
            , end(arg_begin)
            , bad(false)
        {}

        void on_byte(byte) {}

        void on_element(Buffer::iterator i)
        {
            if (begin != end) {
                bad = true;
            }
            end = i;
        }

        util::sptr<Command> spawn_command(util::sref<Client> c, Buffer::iterator)
        {
            if (bad) {
                return util::mkptr(new DirectResponseCommand(
                    "-ERR wrong number of arguments for 'ping' command\r\n", c));
            }
            if (begin == end) {
                return util::mkptr(new DirectResponseCommand("+PONG\r\n", c));
            }
            return util::mkptr(new DirectResponseCommand(Buffer(begin, end), c));
        }
    };

    std::map<std::string, std::function<util::sptr<SpecialCommandParser>(
        Buffer::iterator)>> const SPECIAL_RSP(
    {
        {"PING",
            [](Buffer::iterator arg_start)
            {
                return util::mkptr(new PingCommandParser(arg_start));
            }},
        {"KEYS",
            [](Buffer::iterator)
            {
                return util::mkptr(new ForbiddenCommandParser);
            }},
    });

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

        util::sptr<SpecialCommandParser> special_parser;

        util::sref<Client> client;

        ClientCommandSplitter(Buffer::iterator i, util::sref<Client> cli)
            : BaseType(i)
            , last_command_begin(i)
            , on_byte(std::bind(&ClientCommandSplitter::on_command_byte,
                                this, std::placeholders::_1))
            , on_element(std::bind(&ClientCommandSplitter::on_raw_element,
                                   this, std::placeholders::_1))
            , special_parser(nullptr)
            , client(cli)
        {}

        ClientCommandSplitter(ClientCommandSplitter&& rhs)
            : BaseType(std::move(rhs))
            , last_command_begin(rhs.last_command_begin)
            , last_command(std::move(rhs.last_command))
            , splitted_commands(std::move(rhs.splitted_commands))
            , on_byte(std::move(rhs.on_byte))
            , on_element(std::move(rhs.on_element))
            , special_parser(std::move(rhs.special_parser))
            , client(rhs.client)
        {}

        void on_raw_element(Buffer::iterator i)
        {
            splitted_commands.push_back(quick_rsp(last_command, client));
            last_command_begin = i;
            BaseType::on_element(i);
        }

        void on_command_arr_first_element(Buffer::iterator it)
        {
            auto fi = SPECIAL_RSP.find(last_command);
            if (fi != SPECIAL_RSP.end()) {
                special_parser = fi->second(it);
                this->on_byte =
                    [&](byte b)
                    {
                        this->special_parser->on_byte(b);
                    };
                this->on_element =
                    [&](Buffer::iterator i)
                    {
                        this->special_parser->on_element(i);
                        BaseType::on_element(it);
                    };
            } else {
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
            BaseType::on_element(it);
        }

        void on_command_arr_after_first_element(Buffer::iterator it)
        {
            this->on_byte = [&](byte) {};
            this->on_element =
                [&](Buffer::iterator i)
                {
                    BaseType::on_element(i);
                };
            BaseType::on_element(it);
        }

        void on_command_byte(byte b)
        {
            last_command += std::toupper(b);
        }

        void on_key_byte(byte b)
        {
            if (last_keys.empty()) {
                last_keys.push_back(std::vector<byte>());
            }
            last_keys.back().push_back(b);
        }

        void on_arr_end(Buffer::iterator i)
        {
            this->on_byte = std::bind(&ClientCommandSplitter::on_command_byte,
                                      this, std::placeholders::_1);
            this->on_element = std::bind(&ClientCommandSplitter::on_raw_element,
                                         this, std::placeholders::_1);
            if (special_parser.nul()) {
                splitted_commands.push_back(util::mkptr(new Command(
                    std::move(last_command), std::move(last_keys),
                    last_command_begin, i, client, true)));
            } else {
                splitted_commands.push_back(
                    special_parser->spawn_command(client, i));
                special_parser.reset();
            }
            last_command_begin = i;
            BaseType::on_element(i);
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
            this->on_byte =
                [&](byte b)
                {
                    this->on_command_byte(b);
                };
            this->on_element =
                [&](Buffer::iterator it)
                {
                    this->on_command_arr_first_element(it);
                };
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
