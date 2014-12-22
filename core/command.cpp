#include <sstream>
#include <map>
#include <algorithm>

#include "message.hpp"
#include "command.hpp"
#include "exceptions.hpp"
#include "proxy.hpp"
#include "subscription.hpp"
#include "utils/logging.hpp"
#include "utils/random.hpp"

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

    Server* select_server_for(Proxy* proxy, Command* cmd, slot key_slot)
    {
        Server* svr = proxy->get_server_by_slot(key_slot);
        if (svr == nullptr) {
            LOG(ERROR) << "Cluster slot not covered " << key_slot;
            proxy->retry_move_ask_command_later(util::mkref(*cmd));
            return nullptr;
        }
        svr->push_client_command(util::mkref(*cmd));
        return svr;
    }

    class OneSlotCommand
        : public Command
    {
        slot const key_slot;
    public:
        OneSlotCommand(Buffer b, util::sref<CommandGroup> g, slot ks)
            : Command(std::move(b), g, true)
            , key_slot(ks & (CLUSTER_SLOT_COUNT - 1))
        {}

        Server* select_server(Proxy* proxy)
        {
            return ::select_server_for(proxy, this, this->key_slot);
        }
    };

    class MultiStepsCommand
        : public Command
    {
    public:
        slot current_key_slot;
        std::function<void(Buffer, bool)> on_rsp;

        MultiStepsCommand(util::sref<CommandGroup> group, slot s,
                          std::function<void(Buffer, bool)> r)
            : Command(group, true)
            , current_key_slot(s)
            , on_rsp(std::move(r))
        {}

        Server* select_server(Proxy* proxy)
        {
            return ::select_server_for(proxy, this, this->current_key_slot);
        }

        void copy_response(Buffer rsp, bool error)
        {
            on_rsp(std::move(rsp), error);
        }
    };

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

        Server* select_server(Proxy*)
        {
            return nullptr;
        }
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

    class EachKeyCommandParser
        : public SpecialCommandParser
    {
        slot last_key_slot;
        std::string const command_name;
        std::vector<Buffer::iterator> keys_split_points;
        std::vector<slot> keys_slots;

        virtual Buffer command_header() const = 0;
    public:
        EachKeyCommandParser(Buffer::iterator arg_begin, std::string cmd)
            : last_key_slot(0)
            , command_name(std::move(cmd))
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
                            "-ERR wrong number of arguments for '" +
                            command_name + "' command\r\n", g));
                    });
            }
            util::sptr<CommandGroup> g(new CommandGroup(c));
            for (unsigned i = 0; i < keys_slots.size(); ++i) {
                Buffer b(command_header());
                b.append_from(keys_split_points[i], keys_split_points[i + 1]);
                g->append_command(util::mkptr(
                    new OneSlotCommand(std::move(b), *g, keys_slots[i])));
            }
            return std::move(g);
        }
    };

    class MGetCommandParser
        : public EachKeyCommandParser
    {
        Buffer command_header() const
        {
            return Buffer::from_string("*2\r\n$3\r\nGET\r\n");
        }
    public:
        explicit MGetCommandParser(Buffer::iterator arg_begin)
            : EachKeyCommandParser(arg_begin, "mget")
        {}
    };

    class DelCommandParser
        : public EachKeyCommandParser
    {
        Buffer command_header() const
        {
            return Buffer::from_string("*2\r\n$3\r\nDEL\r\n");
        }
    public:
        explicit DelCommandParser(Buffer::iterator arg_begin)
            : EachKeyCommandParser(arg_begin, "del")
        {}
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
                g->append_command(util::mkptr(new OneSlotCommand(
                    std::move(b), *g, keys_slots[i])));
            }
            return std::move(g);
        }
    };
    std::string const MSetCommandParser::MSetCommandGroup::R("+OK\r\n");

    class RenameCommandParser
        : public SpecialCommandParser
    {
        class RenameCommand
            : public MultiStepsCommand
        {
            Buffer old_key;
            Buffer new_key;
            slot old_key_slot;
            slot new_key_slot;
        public:
            RenameCommand(Buffer old_key, Buffer new_key, slot old_key_slot,
                          slot new_key_slot, util::sref<CommandGroup> group)
                : MultiStepsCommand(group, old_key_slot,
                                    [&](Buffer r, bool e)
                                    {
                                        return this->rsp_get(std::move(r), e);
                                    })
                , old_key(std::move(old_key))
                , new_key(std::move(new_key))
                , old_key_slot(old_key_slot)
                , new_key_slot(new_key_slot)
            {
                this->buffer = Buffer::from_string("*2\r\n$3\r\nGET\r\n");
                this->buffer.append_from(this->old_key.begin(), this->old_key.end());
            }

            void rsp_get(Buffer rsp, bool error)
            {
                if (error) {
                    this->buffer = std::move(rsp);
                    return this->group->command_responsed();
                }
                if (rsp.same_as_string("$-1\r\n")) {
                    this->buffer = Buffer::from_string(
                        "-ERR no such key\r\n");
                    return this->group->command_responsed();
                }
                this->buffer = Buffer::from_string("*3\r\n$3\r\nSET\r\n");
                this->buffer.append_from(new_key.begin(), new_key.end());
                this->buffer.append_from(rsp.begin(), rsp.end());
                this->current_key_slot = new_key_slot;
                this->on_rsp =
                    [&](Buffer rsp, bool error)
                    {
                        if (error) {
                            this->buffer = std::move(rsp);
                            return this->group->command_responsed();
                        }
                        rsp_set();
                    };
                this->group->client->reactivate(util::mkref(*this));
            }

            void rsp_set()
            {
                this->buffer = Buffer::from_string("*2\r\n$3\r\nDEL\r\n");
                this->buffer.append_from(old_key.begin(), old_key.end());
                this->current_key_slot = old_key_slot;
                this->on_rsp =
                    [&](Buffer, bool)
                    {
                        this->buffer = Buffer::from_string("+OK\r\n");
                        this->group->command_responsed();
                    };
                this->group->client->reactivate(util::mkref(*this));
            }
        };

        Buffer::iterator command_begin;
        std::vector<Buffer::iterator> split_points;
        slot key_slot[3];
        int slot_index;
        bool bad;
    public:
        RenameCommandParser(Buffer::iterator cmd_begin,
                            Buffer::iterator arg_begin)
            : command_begin(cmd_begin)
            , slot_index(0)
            , bad(false)
        {
            split_points.push_back(arg_begin);
            key_slot[0] = 0;
            key_slot[1] = 0;
        }

        void on_byte(byte b)
        {
            this->key_slot[slot_index] = crc16(this->key_slot[slot_index], b);
        }

        void on_element(Buffer::iterator i)
        {
            this->split_points.push_back(i);
            key_slot[slot_index] = key_slot[slot_index] & (CLUSTER_SLOT_COUNT - 1);
            if (++slot_index == 3) {
                this->bad = true;
                this->slot_index = 2;
            }
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (slot_index != 2 || this->bad) {
                return only_command(
                    c,
                    [](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'rename' command\r\n", g));
                    });
            }
            LOG(DEBUG) << "#Rename slots: " << key_slot[0] << " - " << key_slot[1];
            if (key_slot[0] == key_slot[1]) {
                return only_command(
                    c,
                    [this](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new OneSlotCommand(
                                Buffer(command_begin, split_points[2]),
                                g, key_slot[0]));
                    });
            }
            return only_command(
                c,
                [this](util::sref<CommandGroup> g)
                {
                    return util::mkptr(new RenameCommand(
                        Buffer(split_points[0], split_points[1]),
                        Buffer(split_points[1], split_points[2]),
                        key_slot[0], key_slot[1], g));
                });
        }
    };

    class SubscribeCommandParser
        : public SpecialCommandParser
    {
        class Subscribe
            : public CommandGroup
        {
            Buffer buffer;
        public:
            explicit Subscribe(Buffer b)
                : buffer(std::move(b))
            {}

            void deliver_client(Proxy* p, Client* client)
            {
                new Subscription(p, client->fd, p->random_addr(),
                                 std::move(buffer));
                LOG(DEBUG) << "Deliver " << client << "'s FD "
                           << client->fd << " as subscription client";
                client->fd = -1;
            }
        };

        Buffer::iterator begin;
        bool no_arg;
    public:
        void on_byte(byte) {}
        void on_element(Buffer::iterator)
        {
            no_arg = false;
        }

        explicit SubscribeCommandParser(Buffer::iterator begin)
            : begin(begin)
            , no_arg(true)
        {}

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end)
        {
            if (no_arg) {
                return only_command(
                    c,
                    [&](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'subscribe' command\r\n", g));
                    });
            }
            return util::mkptr(new Subscribe(Buffer(this->begin, end)));
        }
    };

    class PublishCommandParser
        : public SpecialCommandParser
    {
        Buffer::iterator begin;
        int arg_count;
    public:
        void on_byte(byte) {}
        void on_element(Buffer::iterator)
        {
            ++arg_count;
        }

        explicit PublishCommandParser(Buffer::iterator begin)
            : begin(begin)
            , arg_count(0)
        {}

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end)
        {
            if (arg_count != 2) {
                return only_command(
                    c,
                    [](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR wrong number of arguments for 'publish' command\r\n", g));
                    });
            }
            return only_command(
                c,
                [&](util::sref<CommandGroup> g)
                {
                    return util::mkptr(new OneSlotCommand(
                        Buffer(begin, end), g,
                        util::randint(0, CLUSTER_SLOT_COUNT)));
                });
        }
    };

    std::map<std::string, std::function<util::sptr<SpecialCommandParser>(
        Buffer::iterator, Buffer::iterator)>> const SPECIAL_RSP(
    {
        {"PING",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new PingCommandParser(arg_start));
            }},
        {"DEL",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new DelCommandParser(arg_start));
            }},
        {"MGET",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new MGetCommandParser(arg_start));
            }},
        {"MSET",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new MSetCommandParser(arg_start));
            }},
        {"RENAME",
            [](Buffer::iterator command_begin, Buffer::iterator arg_start)
            {
                return util::mkptr(new RenameCommandParser(
                    command_begin, arg_start));
            }},
        {"SUBSCRIBE",
            [](Buffer::iterator command_begin, Buffer::iterator)
            {
                return util::mkptr(new SubscribeCommandParser(command_begin));
            }},
        {"PSUBSCRIBE",
            [](Buffer::iterator command_begin, Buffer::iterator)
            {
                return util::mkptr(new SubscribeCommandParser(command_begin));
            }},
        {"PUBLISH",
            [](Buffer::iterator command_begin, Buffer::iterator)
            {
                return util::mkptr(new PublishCommandParser(command_begin));
            }},
    });

    /*
    std::set<std::string> UNSUPPORTED_RSP({
        "KEYS", "MIGRATE", "MOVE", "OBJECT", "RANDOMKEY",
        "RENAMENX", "SCAN", "BITOP",
        "BLPOP", "BRPOP", "BRPOPLPUSH", "RPOPLPUSH",
        "SINTERSTORE", "SDIFFSTORE", "SINTER", "SMOVE", "SUNIONSTORE",
        "ZINTERSTORE", "ZUNIONSTORE",
        "PFADD", "PFCOUNT", "PFMERGE",
        "PUBSUB", "PUNSUBSCRIBE", "UNSUBSCRIBE",
        "EVAL", "EVALSHA", "SCRIPT",
        "WATCH", "UNWATCH", "EXEC", "DISCARD", "MULTI",
        "SELECT", "QUIT", "ECHO", "AUTH",
        "CLUSTER", "BGREWRITEAOF", "BGSAVE", "CLIENT", "COMMAND", "CONFIG",
        "DBSIZE", "DEBUG", "FLUSHALL", "FLUSHDB", "INFO", "LASTSAVE", "MONITOR",
        "ROLE", "SAVE", "SHUTDOWN", "SLAVEOF", "SLOWLOG", "SYNC", "TIME",
    });
    */

    /* Name : minimal number of arguments */
    std::map<std::string, int> STD_COMMANDS({
        {"DUMP", 0},
        {"EXISTS", 0},
        {"EXPIRE", 1},
        {"EXPIREAT", 1},
        {"TTL", 0},
        {"PEXPIRE", 1},
        {"PEXPIREAT", 1},
        {"PTTL", 0},
        {"PERSIST", 0},
        {"RESTORE", 2},
        {"TYPE", 0},

        {"GET", 0},
        {"SET", 1},
        {"SETNX", 1},
        {"GETSET", 1},
        {"SETEX", 2},
        {"PSETEX", 2},
        {"SETBIT", 2},
        {"APPEND", 1},
        {"BITCOUNT", 0},
        {"GETBIT", 1},
        {"GETRANGE", 1},
        {"SETRANGE", 2},
        {"STRLEN", 0},
        {"INCR", 0},
        {"DECR", 0},
        {"INCRBY", 1},
        {"DECRBY", 1},
        {"INCRBYFLOAT", 1},

        {"HEXISTS", 1},
        {"HGET", 1},
        {"HGETALL", 0},
        {"HSET", 2},
        {"HSETNX", 2},
        {"HDEL", 1},
        {"HKEYS", 0},
        {"HVALS", 0},
        {"HLEN", 0},
        {"HINCRBY", 2},
        {"HINCRBYFLOAT", 2},
        {"HKEYS", 0},
        {"HMGET", 1},
        {"HMSET", 2},
        {"HSCAN", 1},

        {"LINDEX", 1},
        {"LINSERT", 3},
        {"LLEN", 0},
        {"LPOP", 0},
        {"RPOP", 0},
        {"LPUSH", 1},
        {"LPUSHX", 1},
        {"RPUSH", 1},
        {"RPUSHX", 1},
        {"LRANGE", 2},
        {"LREM", 2},
        {"LSET", 2},
        {"LTRIM", 2},

        {"SCARD", 0},
        {"SADD", 1},
        {"SISMEMBER", 1},
        {"SMEMBERS", 0},
        {"SPOP", 0},
        {"SRANDMEMBER", 0},
        {"SREM", 1},
        {"SSCAN", 1},

        {"ZCARD", 0},
        {"ZADD", 1},
        {"ZREM", 1},
        {"ZSCAN", 1},
        {"ZCOUNT", 2},
        {"ZINCRBY", 2},
        {"ZLEXCOUNT", 2},
        {"ZRANGE", 2},
        {"ZRANGEBYLEX", 2},
        {"ZREVRANGEBYLEX", 2},
        {"ZRANGEBYSCORE", 2},
        {"ZRANK", 1},
        {"ZREMRANGEBYLEX", 2},
        {"ZREMRANGEBYRANK", 2},
        {"ZREMRANGEBYSCORE", 2},
        {"ZREVRANGE", 2},
        {"ZREVRANGEBYSCORE", 2},
        {"ZREVRANK", 1},
        {"ZSCORE", 1},
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
        int last_command_arg_count;

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
            , last_command_arg_count(0)
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
            , last_command_arg_count(rhs.last_command_arg_count)
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

        bool handle_standard_key_command()
        {
            auto i = STD_COMMANDS.find(last_command);
            if (i == STD_COMMANDS.end()) {
                return false;
            }
            this->last_command_arg_count = i->second;
            this->last_command_is_bad = true;
            this->on_byte =
                [this](byte b)
                {
                    this->on_key_byte(b);
                };
            this->on_element =
                [this](Buffer::iterator i)
                {
                    this->on_command_key(i);
                };
            return true;
        }

        void select_command_parser(Buffer::iterator it)
        {
            if (handle_standard_key_command()) {
                return;
            }
            auto sfi = SPECIAL_RSP.find(last_command);
            if (sfi != SPECIAL_RSP.end()) {
                special_parser = sfi->second(last_command_begin, it);
                this->on_byte =
                    [this](byte b)
                    {
                        this->special_parser->on_byte(b);
                    };
                this->on_element =
                    [this](Buffer::iterator i)
                    {
                        this->special_parser->on_element(i);
                        BaseType::on_element(i);
                    };
                return;
            }
            last_command_is_bad = true;
            this->on_byte = [](byte) {};
            this->on_element =
                [this](Buffer::iterator i)
                {
                    BaseType::on_element(i);
                };
        }

        void on_command_arr_first_element(Buffer::iterator it)
        {
            select_command_parser(it);
            BaseType::on_element(it);
        }

        void on_command_key(Buffer::iterator i)
        {
            this->last_command_is_bad = false;
            this->on_byte = [](byte) {};
            this->on_element =
                [this](Buffer::iterator i)
                {
                    --this->last_command_arg_count;
                    BaseType::on_element(i);
                };
            BaseType::on_element(i);
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
                    [](util::sref<CommandGroup> g)
                    {
                        return util::mkptr(new DirectResponseCommand(
                            "-ERR Unknown command or command key not specified\r\n", g));
                    }));
            } else if (special_parser.nul()) {
                if (this->last_command_arg_count > 0) {
                    splitted_groups.push_back(only_command(
                        client,
                        [](util::sref<CommandGroup> g)
                        {
                            return util::mkptr(new DirectResponseCommand(
                                "-ERR wrong number of arguments\r\n", g));
                        }));
                } else {
                    splitted_groups.push_back(only_command(
                        client,
                        [&](util::sref<CommandGroup> g)
                        {
                            return util::mkptr(new OneSlotCommand(
                                Buffer(last_command_begin, i), g, last_key_slot));
                        }));
                }
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
                [this](byte b)
                {
                    this->on_command_byte(b);
                };
            this->on_element =
                [this](Buffer::iterator it)
                {
                    this->on_command_arr_first_element(it);
                };
        }
    };

}

void Command::copy_response(Buffer rsp, bool)
{
    this->buffer = std::move(rsp);
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
    if (c.finished()) {
        buffer.clear();
    } else {
        buffer.truncate_from_begin(c.interrupt_point());
    }
    return std::move(c.splitted_groups);
}
