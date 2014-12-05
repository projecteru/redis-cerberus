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

    uint16_t const CRC16TAB[] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
        0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
        0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
        0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
        0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
        0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
        0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
        0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
        0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
        0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
        0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
        0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
        0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
        0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
        0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
        0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
        0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
        0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
        0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
        0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
        0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
        0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
    };

    slot crc16(slot crc, byte next_byte)
    {
        return (crc << 8) ^ CRC16TAB[((crc >> 8) ^ next_byte) & 0xFF];
    }

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
            : Command(std::move(b), g, false, 0xFFFF)
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
        std::vector<slot> keys_slots;
        slot last_key_slot;
    public:
        explicit MGetCommandParser(Buffer::iterator arg_begin)
            : last_key_slot(0)
        {
            keys_split_points.push_back(arg_begin);
        }

        void on_byte(byte b)
        {
            last_key_slot = crc16(last_key_slot, b);
        }

        void on_element(Buffer::iterator i)
        {
            keys_slots.push_back(last_key_slot);
            last_key_slot = 0;
            keys_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (keys_slots.empty()) {
                return only_command(
                    c,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'mget' command\r\n", g));
                    });
            }
            util::sptr<CommandGroup> g(new CommandGroup(c));
            for (unsigned i = 0; i < keys_slots.size(); ++i) {
                Buffer b(Buffer::from_string("*2\r\n$3\r\nGET\r\n"));
                b.append_from(keys_split_points[i], keys_split_points[i + 1]);
                g->append_command(util::mkptr(
                    new Command(std::move(b), *g, true, keys_slots[i])));
            }
            return std::move(g);
        }
    };

    class MSetCommandParser
        : public SpecialCommandParser
    {
        class MSetCommandGroup
            : public CommandGroup
        {
            static std::string const R;
        public:
            explicit MSetCommandGroup(util::sref<Client> c)
                : CommandGroup(c)
            {}

            void append_buffer_to(std::vector<struct iovec>& iov)
            {
                arr_payload = Buffer::from_string(R);
                arr_payload.buffer_ready(iov);
            }

            int total_buffer_size() const
            {
                return R.size();
            }
        };

        std::vector<Buffer::iterator> kv_split_points;
        std::vector<slot> keys_slots;
        slot last_key_slot;
        bool current_is_key;
    public:
        explicit MSetCommandParser(Buffer::iterator arg_begin)
            : last_key_slot(0)
            , current_is_key(true)
        {
            kv_split_points.push_back(arg_begin);
        }

        void on_byte(byte b)
        {
            if (current_is_key) {
                last_key_slot = crc16(last_key_slot, b);
            }
        }

        void on_element(Buffer::iterator i)
        {
            if (current_is_key) {
                keys_slots.push_back(last_key_slot);
                last_key_slot = 0;
            }
            current_is_key = !current_is_key;
            kv_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (keys_slots.empty() || !current_is_key)
            {
                return only_command(
                    c,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'mset' command\r\n", g));
                    });
            }
            util::sptr<CommandGroup> g(new MSetCommandGroup(c));
            for (unsigned i = 0; i < keys_slots.size(); ++i) {
                Buffer b(Buffer::from_string("*3\r\n$3\r\nSET\r\n"));
                b.append_from(kv_split_points[i * 2], kv_split_points[i * 2 + 2]);
                g->append_command(util::mkptr(new Command(std::move(b), *g, true,
                                  keys_slots[i])));
            }
            return std::move(g);
        }
    };
    std::string const MSetCommandParser::MSetCommandGroup::R("+OK\r\n");

    std::map<std::string, std::function<util::sptr<SpecialCommandParser>(
        Buffer::iterator)>> const SPECIAL_RSP(
    {
        {"PING",
            [](Buffer::iterator arg_start)
            {
                return util::mkptr(new PingCommandParser(arg_start));
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

    std::set<std::string> UNSUPPORTED_RSP({
        "KEYS", "MIGRATE", "MOVE", "OBJECT", "RANDOMKEY", "RENAME",
        "RENAMENX", "SCAN", "BITOP",
        "BLPOP", "BRPOP", "BRPOPLPUSH", "RPOPLPUSH",
        "SINTERSTORE", "SDIFFSTORE", "SINTER", "SMOVE", "SUNIONSTORE",
        "ZINTERSTORE", "ZUNIONSTORE",
        "PFADD", "PFCOUNT", "PFMERGE",
        "PSUBSCRIBE", "PUBSUB", "PUBLISH", "PUNSUBSCRIBE", "SUBSCRIBE", "UNSUBSCRIBE",
        "EVAL", "EVALSHA", "SCRIPT",
        "WATCH", "UNWATCH", "EXEC", "DISCARD", "MULTI",
        "SELECT", "QUIT", "ECHO", "AUTH",
        "CLUSTER", "BGREWRITEAOF", "BGSAVE", "CLIENT", "COMMAND", "CONFIG",
        "DBSIZE", "DEBUG", "FLUSHALL", "FLUSHDB", "INFO", "LASTSAVE", "MONITOR",
        "ROLE", "SAVE", "SHUTDOWN", "SLAVEOF", "SLOWLOG", "SYNC", "TIME",
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
        slot last_key_slot;
        bool last_command_is_bad;

        std::vector<util::sptr<CommandGroup>> splitted_groups;
        std::function<void(byte)> on_byte;
        std::function<void(Buffer::iterator)> on_element;

        util::sptr<SpecialCommandParser> special_parser;

        util::sref<Client> client;

        ClientCommandSplitter(Buffer::iterator i, util::sref<Client> cli)
            : BaseType(i)
            , last_command_begin(i)
            , last_key_slot(0)
            , last_command_is_bad(false)
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
            , last_key_slot(rhs.last_key_slot)
            , last_command_is_bad(rhs.last_command_is_bad)
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
            auto sfi = SPECIAL_RSP.find(last_command);
            if (sfi != SPECIAL_RSP.end()) {
                special_parser = sfi->second(it);
                this->on_byte =
                    [&](byte b)
                    {
                        this->special_parser->on_byte(b);
                    };
                this->on_element =
                    [&](Buffer::iterator i)
                    {
                        this->special_parser->on_element(i);
                        BaseType::on_element(i);
                    };
            }
            else if (UNSUPPORTED_RSP.find(last_command) != UNSUPPORTED_RSP.end())
            {
                last_command_is_bad = true;
                this->on_byte = [](byte) {};
                this->on_element =
                    [&](Buffer::iterator i)
                    {
                        BaseType::on_element(i);
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
            last_key_slot = crc16(last_key_slot, b);
        }

        void on_arr_end(Buffer::iterator i)
        {
            this->on_byte = std::bind(&ClientCommandSplitter::on_command_byte,
                                      this, std::placeholders::_1);
            this->on_element = std::bind(&ClientCommandSplitter::on_raw_element,
                                         this, std::placeholders::_1);
            if (last_command_is_bad) {
                splitted_groups.push_back(only_command(
                    client,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR Unsupported command\r\n", g));
                    }));
            } else if (special_parser.nul()) {
                splitted_groups.push_back(only_command(
                    client,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new Command(
                            Buffer(last_command_begin, i), g, true, last_key_slot));
                    }));
            } else {
                splitted_groups.push_back(special_parser->spawn_commands(client, i));
                special_parser.reset();
            }
            last_command.clear();
            last_command_begin = i;
            last_key_slot = 0;
            last_command_is_bad = false;
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
}

void CommandGroup::append_command(util::sptr<Command> c)
{
    if (c->need_send) {
        awaiting_count += 1;
    }
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
