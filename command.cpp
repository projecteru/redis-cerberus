#include <sstream>
#include <map>
#include <algorithm>

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
        DirectResponseCommand(std::string const& r, util::sref<CommandGroup> g)
            : DirectResponseCommand(Buffer::from_string(r), g)
        {}

        DirectResponseCommand(char const* r, util::sref<CommandGroup> g)
            : DirectResponseCommand(Buffer::from_string(r), g)
        {}

        DirectResponseCommand(Buffer b, util::sref<CommandGroup> g)
            : Command(std::move(b), g, false)
        {}
    };

    std::map<std::string, std::string> const QUICK_RSP({
        {"PING", "+PONG\r\n"},
    });

    util::sptr<CommandGroup> quick_rsp(std::string const& command, util::sref<Client> c)
    {
        util::sptr<CommandGroup> g(new CommandGroup(c));
        auto fi = QUICK_RSP.find(command);
        if (fi == QUICK_RSP.end()) {
            g->append_command(util::mkptr(new DirectResponseCommand(
                "-ERR unknown command '" + command + "'\r\n", *g)));
        } else {
            g->append_command(util::mkptr(new DirectResponseCommand(
                fi->second, *g)));
        }
        return std::move(g);
    }

    template <typename SpawnF>
    util::sptr<CommandGroup> only_command(util::sref<Client> c, SpawnF f)
    {
        util::sptr<CommandGroup> g(new CommandGroup(c));
        g->append_command(f(*g));
        return std::move(g);
    }

    class SpecialCommandParser {
    public:
        virtual void on_byte(byte b) = 0;
        virtual void on_element(Buffer::iterator i) = 0;
        virtual ~SpecialCommandParser() {}

        virtual util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end) = 0;

        SpecialCommandParser() {}
        SpecialCommandParser(SpecialCommandParser const&) = delete;
    };

    class ForbiddenCommandParser
        : public SpecialCommandParser
    {
    public:
        ForbiddenCommandParser() {}

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            return only_command(
                c,
                [&](util::sref<CommandGroup> g)
                {
                    return util::mkptr(new DirectResponseCommand(
                        "-ERR this command is forbidden in cluster\r\n", g));
                });
        }

        void on_byte(byte) {}
        void on_element(Buffer::iterator) {}
    };

    class PingCommandParser
        : public SpecialCommandParser
    {
        Buffer::iterator begin;
        Buffer::iterator end;
        bool bad;
    public:
        explicit PingCommandParser(Buffer::iterator arg_begin)
            : begin(arg_begin)
            , end(arg_begin)
            , bad(false)
        {}

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end)
        {
            return only_command(
                c,
                [&](util::sref<CommandGroup> g)
                {
                    if (bad) {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'ping' command\r\n", g));
                    }
                    if (begin == end) {
                        return util::mkptr(new DirectResponseCommand("+PONG\r\n", g));
                    }
                    return util::mkptr(new DirectResponseCommand(Buffer(begin, end), g));
                });
        }

        void on_byte(byte) {}

        void on_element(Buffer::iterator i)
        {
            if (begin != end) {
                bad = true;
            }
            end = i;
        }
    };

    class MGetCommandParser
        : public SpecialCommandParser
    {
        std::vector<Buffer::iterator> keys_split_points;
    public:
        explicit MGetCommandParser(Buffer::iterator arg_begin)
        {
            keys_split_points.push_back(arg_begin);
        }

        void on_byte(byte) {}

        void on_element(Buffer::iterator i)
        {
            keys_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (keys_split_points.size() == 1) {
                return only_command(
                    c,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'mget' command\r\n", g));
                    });
            }
            util::sptr<CommandGroup> g(new CommandGroup(c));
            for (auto i = keys_split_points.begin() + 1;
                 i != keys_split_points.end(); ++i)
            {
                Buffer b(Buffer::from_string("*2\r\n$3\r\nGET\r\n"));
                b.append_from(*(i - 1), *i);
                g->append_command(util::mkptr(new Command(std::move(b), *g, true)));
            }
            return std::move(g);
        }
    };

    class MSetCommandParser
        : public SpecialCommandParser
    {
        std::vector<Buffer::iterator> kv_split_points;
    public:
        explicit MSetCommandParser(Buffer::iterator arg_begin)
        {
            kv_split_points.push_back(arg_begin);
        }

        void on_byte(byte) {}

        void on_element(Buffer::iterator i)
        {
            kv_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (kv_split_points.size() == 1 || kv_split_points.size() % 2 != 1)
            {
                return only_command(
                    c,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'mset' command\r\n", g));
                    });
            }
            util::sptr<CommandGroup> g(new CommandGroup(c));
            for (auto i = kv_split_points.begin() + 1;
                 i != kv_split_points.end(); i += 2)
            {
                Buffer b(Buffer::from_string("*3\r\n$3\r\nSET\r\n"));
                b.append_from(*(i - 1), *(i + 1));
                g->append_command(util::mkptr(new Command(std::move(b), *g, true)));
            }
            return std::move(g);
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
        {"MGET",
            [](Buffer::iterator arg_start)
            {
                return util::mkptr(new MGetCommandParser(arg_start));
            }},
        {"MSET",
            [](Buffer::iterator arg_start)
            {
                return util::mkptr(new MSetCommandParser(arg_start));
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

        std::vector<util::sptr<CommandGroup>> splitted_groups;
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
            , splitted_groups(std::move(rhs.splitted_groups))
            , on_byte(std::move(rhs.on_byte))
            , on_element(std::move(rhs.on_element))
            , special_parser(std::move(rhs.special_parser))
            , client(rhs.client)
        {}

        void on_raw_element(Buffer::iterator i)
        {
            splitted_groups.push_back(quick_rsp(last_command, client));
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

        void on_key_byte(byte) {}

        void on_arr_end(Buffer::iterator i)
        {
            this->on_byte = std::bind(&ClientCommandSplitter::on_command_byte,
                                      this, std::placeholders::_1);
            this->on_element = std::bind(&ClientCommandSplitter::on_raw_element,
                                         this, std::placeholders::_1);
            if (special_parser.nul()) {
                splitted_groups.push_back(only_command(
                    client,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new Command(
                            Buffer(last_command_begin, i), g, true));
                    }));
                last_command.clear();
            } else {
                splitted_groups.push_back(special_parser->spawn_commands(client, i));
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
    };

}

void Command::copy_response(Buffer::iterator begin, Buffer::iterator end)
{
    this->buffer.copy_from(begin, end);
    this->group->command_responsed();
}

void CommandGroup::command_responsed()
{
    if (--awaiting_count == 0) {
        if (1 < commands.size()) {
            std::stringstream ss;
            ss << "*" << commands.size() << "\r\n";
            arr_payload = Buffer::from_string(ss.str());
        }
        client->group_responsed();
    }
    // printf("-g await %d %c\n", awaiting_count, awaiting_count == 0 ? 'O' : 'X');
}

void CommandGroup::append_command(util::sptr<Command> c)
{
    if (c->need_send) {
        awaiting_count += 1;
    }
    // printf("-g append %c %d\n", c->need_send ? '+' : ' ', awaiting_count);
    commands.push_back(std::move(c));
}

void CommandGroup::append_buffer_to(std::vector<struct iovec>& iov)
{
    arr_payload.buffer_ready(iov);
    std::for_each(commands.begin(), commands.end(),
                  [&](util::sptr<Command>& command)
                  {
                      command->buffer.buffer_ready(iov);
                  });
}

int CommandGroup::total_buffer_size() const
{
    int i = arr_payload.size();
    std::for_each(commands.begin(), commands.end(),
                  [&](util::sptr<Command> const& command)
                  {
                      i += command->buffer.size();
                  });
    return i;
}

std::vector<util::sptr<CommandGroup>> cerb::split_client_command(
    Buffer& buffer, util::sref<Client> cli)
{
    ClientCommandSplitter c(cerb::msg::split_by(
        buffer.begin(), buffer.end(), ClientCommandSplitter(
            buffer.begin(), cli)));
    return std::move(c.splitted_groups);
}
