#include <sstream>
#include <algorithm>

#include "message.hpp"
#include "command.hpp"
#include "exceptions.hpp"
#include "proxy.hpp"
#include "client.hpp"
#include "server.hpp"
#include "subscription.hpp"
#include "stats.hpp"
#include "slot_calc.hpp"
#include "globals.hpp"
#include "utils/logging.hpp"
#include "utils/random.hpp"
#include "utils/string.h"

using namespace cerb;

namespace {

    std::string const RSP_OK_STR("+OK\r\n");
    Buffer RSP_OK(Buffer::from_string(RSP_OK_STR));

    Server* select_server_for(Proxy* proxy, DataCommand* cmd, slot key_slot)
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
        : public DataCommand
    {
        slot const key_slot;
    public:
        OneSlotCommand(Buffer b, util::sref<CommandGroup> g, slot ks)
            : DataCommand(std::move(b), g)
            , key_slot(ks)
        {
            LOG(DEBUG) << "-Keyslot = " << this->key_slot;
        }

        Server* select_server(Proxy* proxy)
        {
            return ::select_server_for(proxy, this, this->key_slot);
        }
    };

    class MultiStepsCommand
        : public DataCommand
    {
    public:
        slot current_key_slot;
        std::function<void(Buffer, bool)> on_rsp;

        MultiStepsCommand(util::sref<CommandGroup> group, slot s,
                          std::function<void(Buffer, bool)> r)
            : DataCommand(group)
            , current_key_slot(s)
            , on_rsp(std::move(r))
        {}

        Server* select_server(Proxy* proxy)
        {
            return ::select_server_for(proxy, this, this->current_key_slot);
        }

        void on_remote_responsed(Buffer rsp, bool error)
        {
            on_rsp(std::move(rsp), error);
        }
    };

    class DirectCommandGroup
        : public CommandGroup
    {
        class DirectCommand
            : public Command
        {
        public:
            DirectCommand(Buffer b, util::sref<CommandGroup> g)
                : Command(std::move(b), g)
            {}

            Server* select_server(Proxy*)
            {
                return nullptr;
            }
        };
    public:
        util::sptr<DirectCommand> command;

        DirectCommandGroup(util::sref<Client> client, Buffer b)
            : CommandGroup(client)
            , command(new DirectCommand(std::move(b), util::mkref(*this)))
        {}

        DirectCommandGroup(util::sref<Client> client, char const* r)
            : DirectCommandGroup(client, Buffer::from_string(r))
        {}

        DirectCommandGroup(util::sref<Client> client, std::string r)
            : DirectCommandGroup(client, Buffer::from_string(r))
        {}

        bool wait_remote() const
        {
            return false;
        }

        void select_remote(Proxy*) {}

        void append_buffer_to(std::vector<util::sref<Buffer>>& b)
        {
            b.push_back(util::mkref(command->buffer));
        }

        int total_buffer_size() const
        {
            return command->buffer.size();
        }

        void command_responsed() {}
    };

    class StatsCommandGroup
        : public CommandGroup
    {
    protected:
        explicit StatsCommandGroup(util::sref<Client> cli)
            : CommandGroup(cli)
            , creation(Clock::now())
            , complete(false)
        {}

        Time const creation;
        bool complete;

        bool wait_remote() const
        {
            return true;
        }

        void collect_stats(Proxy* p) const
        {
            p->stat_proccessed(Clock::now() - this->creation,
                               this->avg_commands_remote_cost());
        }

        virtual Interval avg_commands_remote_cost() const = 0;
    };

    class SingleCommandGroup
        : public StatsCommandGroup
    {
    public:
        util::sptr<DataCommand> command;

        explicit SingleCommandGroup(util::sref<Client> cli)
            : StatsCommandGroup(cli)
            , command(nullptr)
        {}

        SingleCommandGroup(util::sref<Client> cli, Buffer b, slot ks)
            : StatsCommandGroup(cli)
            , command(new OneSlotCommand(std::move(b), util::mkref(*this), ks))
        {}

        void command_responsed()
        {
            client->group_responsed();
            this->complete = true;
        }

        void append_buffer_to(std::vector<util::sref<Buffer>>& b)
        {
            b.push_back(util::mkref(command->buffer));
        }

        int total_buffer_size() const
        {
            return command->buffer.size();
        }

        void select_remote(Proxy* proxy)
        {
            command->select_server(proxy);
        }

        Interval avg_commands_remote_cost() const
        {
            return command->remote_cost();
        }
    };

    class MultipleCommandsGroup
        : public StatsCommandGroup
    {
    public:
        Buffer arr_payload;
        std::vector<util::sptr<DataCommand>> commands;
        int awaiting_count;

        explicit MultipleCommandsGroup(util::sref<Client> c)
            : StatsCommandGroup(c)
            , awaiting_count(0)
        {}

        void append_command(util::sptr<DataCommand> c)
        {
            awaiting_count += 1;
            commands.push_back(std::move(c));
        }

        void command_responsed()
        {
            if (--awaiting_count == 0) {
                if (1 < commands.size()) {
                    std::stringstream ss;
                    ss << "*" << commands.size() << "\r\n";
                    arr_payload = Buffer::from_string(ss.str());
                }
                client->group_responsed();
                this->complete = true;
            }
        }

        void append_buffer_to(std::vector<util::sref<Buffer>>& b)
        {
            b.push_back(util::mkref(arr_payload));
            for (auto const& c: this->commands) {
                b.push_back(util::mkref(c->buffer));
            }
        }

        int total_buffer_size() const
        {
            int i = arr_payload.size();
            for (auto const& c: this->commands) {
                i += c->buffer.size();
            }
            return i;
        }

        void select_remote(Proxy* proxy)
        {
            for (auto& c: this->commands) {
                c->select_server(proxy);
            }
        }

        Interval avg_commands_remote_cost() const
        {
            if (this->commands.empty()) {
                return Interval(0);
            }
            return std::accumulate(
                this->commands.begin(), this->commands.end(), Interval(0),
                [](Interval a, util::sptr<DataCommand> const& c)
                {
                    return a + c->remote_cost();
                }) / this->commands.size();
        }
    };

    class LongCommandGroup
        : public CommandGroup
    {
    public:
        LongCommandGroup(util::sref<Client> client)
            : CommandGroup(client)
        {}

        bool long_connection() const
        {
            return true;
        }

        bool wait_remote() const
        {
            return false;
        }

        int total_buffer_size() const
        {
            return 0;
        }

        void select_remote(Proxy*) {}
        void append_buffer_to(std::vector<util::sref<Buffer>>&) {}
        void command_responsed() {}
    };

    std::string stats_string()
    {
        std::string s(stats_all());
        return '+' + s + "\r\n";
    }

    void notify_each_thread_update_slot_map()
    {
        for (auto& t: cerb_global::all_threads) {
            t.get_proxy()->update_slot_map();
        }
    }

    void notify_each_thread_update_remotes(std::set<util::Address> const& remotes)
    {
        for (auto& t: cerb_global::all_threads) {
            t.get_proxy()->update_remotes(remotes);
        }
    }

    std::map<std::string, std::function<std::string()>> const QUICK_RSP({
        {"PING", []() {return "+PONG\r\n";}},
        {"PROXY", []() {return stats_string();}},
        {"INFO", []() {return stats_string();}},
        {"UPDATESLOTMAP",
            []()
            {
                ::notify_each_thread_update_slot_map();
                return RSP_OK_STR;
            }},
    });

    util::sptr<CommandGroup> quick_rsp(std::string const& command, util::sref<Client> c)
    {
        auto fi = QUICK_RSP.find(command);
        return util::mkptr(new DirectCommandGroup(
            c, fi == QUICK_RSP.end()
                ? "-ERR unknown command '" + command + "'\r\n"
                : fi->second()));
    }

    class SpecialCommandParser {
    public:
        virtual void on_byte(byte b) = 0;
        virtual void on_element(Buffer::iterator i) = 0;
        virtual ~SpecialCommandParser() {}

        virtual util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end) = 0;

        SpecialCommandParser() = default;
        SpecialCommandParser(SpecialCommandParser const&) = delete;
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
            if (bad) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'PING' command\r\n"));
            }
            if (this->begin == end) {
                return util::mkptr(new DirectCommandGroup(c, "+PONG\r\n"));
            }
            return util::mkptr(new DirectCommandGroup(c, Buffer(begin, end)));
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

    class ProxyStatsCommandParser
        : public SpecialCommandParser
    {
    public:
        ProxyStatsCommandParser() = default;

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            return util::mkptr(new DirectCommandGroup(c, stats_string()));
        }

        void on_byte(byte) {}
        void on_element(Buffer::iterator) {}
    };

    class UpdateSlotMapCommandParser
        : public SpecialCommandParser
    {
    public:
        UpdateSlotMapCommandParser() = default;

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            ::notify_each_thread_update_slot_map();
            return util::mkptr(new DirectCommandGroup(c, RSP_OK_STR));
        }

        void on_byte(byte) {}
        void on_element(Buffer::iterator) {}
    };

    class SetRemotesCommandParser
        : public SpecialCommandParser
    {
        std::set<util::Address> remotes;
        std::string last_host;
        int last_port;
        bool current_is_host;
        bool bad;
    public:
        SetRemotesCommandParser()
            : last_port(0)
            , current_is_host(true)
            , bad(false)
        {}

        util::sptr<CommandGroup> spawn_commands(util::sref<Client> c, Buffer::iterator)
        {
            if (this->bad) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR invalid port number\r\n"));
            }
            if (this->remotes.empty() || !this->current_is_host) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'SETREMOTES' command\r\n"));
            }
            ::notify_each_thread_update_remotes(this->remotes);
            return util::mkptr(new DirectCommandGroup(c, RSP_OK_STR));
        }

        void on_byte(byte b)
        {
            if (this->bad) {
                return;
            }
            if (this->current_is_host) {
                this->last_host.push_back(b);
            } else {
                if (b < '0' || '9' < b) {
                    bad = true;
                    return;
                }
                this->last_port = this->last_port * 10 + b - '0';
            }
        }

        void on_element(Buffer::iterator)
        {
            if (!this->current_is_host) {
                this->remotes.insert(util::Address(std::move(this->last_host), this->last_port));
                this->last_port = 0;
            }
            this->current_is_host = !this->current_is_host;
        }
    };

    class EachKeyCommandParser
        : public SpecialCommandParser
    {
        KeySlotCalc slot_calc;
        std::string const command_name;
        std::vector<Buffer::iterator> keys_split_points;
        std::vector<slot> keys_slots;

        virtual Buffer command_header() const = 0;
    public:
        EachKeyCommandParser(Buffer::iterator arg_begin, std::string cmd)
            : command_name(std::move(cmd))
        {
            keys_split_points.push_back(arg_begin);
        }

        void on_byte(byte b)
        {
            slot_calc.next_byte(b);
        }

        void on_element(Buffer::iterator i)
        {
            keys_slots.push_back(slot_calc.get_slot());
            slot_calc.reset();
            keys_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (keys_slots.empty()) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for '" + command_name + "' command\r\n"));
            }
            util::sptr<MultipleCommandsGroup> g(new MultipleCommandsGroup(c));
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
            : public MultipleCommandsGroup
        {
        public:
            explicit MSetCommandGroup(util::sref<Client> c)
                : MultipleCommandsGroup(c)
            {}

            void append_buffer_to(std::vector<util::sref<Buffer>>& b)
            {
                b.push_back(util::mkref(RSP_OK));
            }

            int total_buffer_size() const
            {
                return RSP_OK.size();
            }
        };

        std::vector<Buffer::iterator> kv_split_points;
        std::vector<slot> keys_slots;
        KeySlotCalc slot_calc;
        bool current_is_key;
    public:
        explicit MSetCommandParser(Buffer::iterator arg_begin)
            : current_is_key(true)
        {
            kv_split_points.push_back(arg_begin);
        }

        void on_byte(byte b)
        {
            if (current_is_key) {
                slot_calc.next_byte(b);
            }
        }

        void on_element(Buffer::iterator i)
        {
            if (current_is_key) {
                keys_slots.push_back(slot_calc.get_slot());
                slot_calc.reset();
            }
            current_is_key = !current_is_key;
            kv_split_points.push_back(i);
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (keys_slots.empty() || !current_is_key) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'mset' command\r\n"));
            }
            util::sptr<MSetCommandGroup> g(new MSetCommandGroup(c));
            for (unsigned i = 0; i < keys_slots.size(); ++i) {
                Buffer b(Buffer::from_string("*3\r\n$3\r\nSET\r\n"));
                b.append_from(kv_split_points[i * 2], kv_split_points[i * 2 + 2]);
                g->append_command(util::mkptr(new OneSlotCommand(
                    std::move(b), *g, keys_slots[i])));
            }
            return std::move(g);
        }
    };

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
                    return this->responsed();
                }
                if (rsp.same_as_string("$-1\r\n")) {
                    this->buffer = Buffer::from_string(
                        "-ERR no such key\r\n");
                    return this->responsed();
                }
                this->buffer = Buffer::from_string("*3\r\n$3\r\nSET\r\n");
                this->buffer.append_from(new_key.begin(), new_key.end());
                this->buffer.append_from(rsp.begin(), rsp.end());
                this->current_key_slot = new_key_slot;
                this->on_rsp =
                    [this](Buffer rsp, bool error)
                    {
                        if (error) {
                            this->buffer = std::move(rsp);
                            return this->responsed();
                        }
                        this->rsp_set();
                    };
                this->group->client->reactivate(util::mkref(*this));
            }

            void rsp_set()
            {
                this->buffer = Buffer::from_string("*2\r\n$3\r\nDEL\r\n");
                this->buffer.append_from(old_key.begin(), old_key.end());
                this->current_key_slot = old_key_slot;
                this->on_rsp =
                    [this](Buffer, bool)
                    {
                        this->buffer = Buffer::from_string("+OK\r\n");
                        this->responsed();
                    };
                this->group->client->reactivate(util::mkref(*this));
            }
        };

        Buffer::iterator command_begin;
        std::vector<Buffer::iterator> split_points;
        KeySlotCalc key_slot[3];
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
        }

        void on_byte(byte b)
        {
            this->key_slot[slot_index].next_byte(b);
        }

        void on_element(Buffer::iterator i)
        {
            this->split_points.push_back(i);
            if (++slot_index == 3) {
                this->bad = true;
                this->slot_index = 2;
            }
        }

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator)
        {
            if (slot_index != 2 || this->bad) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'rename' command\r\n"));
            }
            slot src_slot = key_slot[0].get_slot();
            slot dst_slot = key_slot[1].get_slot();
            LOG(DEBUG) << "#Rename slots: " << src_slot << " - " << dst_slot;
            if (src_slot == dst_slot) {
                return util::mkptr(new SingleCommandGroup(
                    c, Buffer(command_begin, split_points[2]), src_slot));
            }
            util::sptr<SingleCommandGroup> g(new SingleCommandGroup(c));
            g->command = util::mkptr(new RenameCommand(
                Buffer(split_points[0], split_points[1]),
                Buffer(split_points[1], split_points[2]),
                src_slot, dst_slot, *g));
            return std::move(g);
        }
    };

    class SubscribeCommandParser
        : public SpecialCommandParser
    {
        class Subscribe
            : public LongCommandGroup
        {
            Buffer buffer;
        public:
            Subscribe(util::sref<Client> client, Buffer b)
                : LongCommandGroup(client)
                , buffer(std::move(b))
            {}

            void deliver_client(Proxy* p)
            {
                Server* s = p->random_addr();
                if (s == nullptr) {
                    return this->client->close();
                }
                new Subscription(p, this->client->fd, s, std::move(buffer));
                LOG(DEBUG) << "Deliver " << client.id().str() << "'s FD "
                           << this->client->fd << " as subscription client";
                this->client->fd = -1;
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
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'subscribe' command\r\n"));
            }
            return util::mkptr(new Subscribe(c, Buffer(this->begin, end)));
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
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong number of arguments for 'publish' command\r\n"));
            }
            return util::mkptr(new SingleCommandGroup(
                c, Buffer(begin, end), util::randint(0, CLUSTER_SLOT_COUNT)));
        }
    };

    class KeysInSlotParser
        : public SpecialCommandParser
    {
        Buffer::iterator _arg_start;
        int _arg_count;
        int _slot;
        bool _error;

        std::function<void(KeysInSlotParser*, byte)> _on_byte;

        static void _calc_slot(KeysInSlotParser* me, byte b)
        {
            if (b >= '0' && b <= '9') {
                me->_slot = me->_slot * 10 + (b - '0');
            } else {
                me->_on_byte = KeysInSlotParser::_skip_byte;
                me->_error = true;
            }
        }

        static void _skip_byte(KeysInSlotParser*, byte) {}
    public:
        void on_byte(byte b)
        {
            this->_on_byte(this, b);
        }

        void on_element(Buffer::iterator)
        {
            ++_arg_count;
            this->_on_byte = KeysInSlotParser::_skip_byte;
        }

        explicit KeysInSlotParser(Buffer::iterator arg_start)
            : _arg_start(arg_start)
            , _arg_count(0)
            , _slot(0)
            , _error(false)
            , _on_byte(KeysInSlotParser::_calc_slot)
        {}

        util::sptr<CommandGroup> spawn_commands(
            util::sref<Client> c, Buffer::iterator end)
        {
            if (_arg_count != 2 || _error || _slot >= CLUSTER_SLOT_COUNT) {
                return util::mkptr(new DirectCommandGroup(
                    c, "-ERR wrong arguments for 'keysinslot' command\r\n"));
            }
            Buffer buffer(Buffer::from_string("*4\r\n$7\r\nCLUSTER\r\n$13\r\nGETKEYSINSLOT\r\n"));
            buffer.append_from(_arg_start, end);
            return util::mkptr(new SingleCommandGroup(c, std::move(buffer), _slot));
        }
    };

    std::map<std::string, std::function<util::sptr<SpecialCommandParser>(
        Buffer::iterator, Buffer::iterator)>> SPECIAL_RSP(
    {
        {"PING",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new PingCommandParser(arg_start));
            }},
        {"INFO",
            [](Buffer::iterator, Buffer::iterator)
            {
                return util::mkptr(new ProxyStatsCommandParser);
            }},
        {"PROXY",
            [](Buffer::iterator, Buffer::iterator)
            {
                return util::mkptr(new ProxyStatsCommandParser);
            }},
        {"UPDATESLOTMAP",
            [](Buffer::iterator, Buffer::iterator)
            {
                return util::mkptr(new UpdateSlotMapCommandParser);
            }},
        {"SETREMOTES",
            [](Buffer::iterator, Buffer::iterator)
            {
                return util::mkptr(new SetRemotesCommandParser);
            }},
        {"MGET",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new MGetCommandParser(arg_start));
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
    });

    std::set<std::string> STD_COMMANDS({
        "DUMP", "EXISTS", "TTL", "PTTL", "TYPE",
        "GET", "BITCOUNT", "GETBIT", "GETRANGE", "STRLEN",
        "HGET", "HGETALL", "HKEYS", "HVALS", "HLEN", "HEXISTS", "HMGET", "HSCAN",
        "LINDEX", "LLEN", "LRANGE",
        "SCARD", "SISMEMBER", "SMEMBERS", "SSCAN",

        "ZCARD", "ZSCAN", "ZCOUNT", "ZLEXCOUNT", "ZRANGE",
        "ZRANGEBYLEX", "ZREVRANGEBYLEX", "ZRANGEBYSCORE", "ZRANK",
        "ZREVRANGE", "ZREVRANGEBYSCORE", "ZREVRANK", "ZSCORE",
    });

    class ClientCommandSplitter
        : public cerb::msg::MessageSplitterBase<
            Buffer::iterator, ClientCommandSplitter>
    {
        typedef cerb::msg::MessageSplitterBase<
            Buffer::iterator, ClientCommandSplitter> BaseType;

        std::function<void(ClientCommandSplitter&, byte)> _on_byte;
        std::function<void(ClientCommandSplitter&, Buffer::iterator)> _on_element;

        void _call_base_on_element(Buffer::iterator it)
        {
            BaseType::on_element(it);
        }

        static void on_raw_element(ClientCommandSplitter& s, Buffer::iterator i)
        {
            s.client->push_command(quick_rsp(s.last_command, s.client));
            s.last_command_begin = i;
            s._call_base_on_element(i);
        }

        static void on_command_arr_first_element(ClientCommandSplitter& s, Buffer::iterator it)
        {
            s.select_command_parser(it);
            s._call_base_on_element(it);
        }

        static void on_command_key(ClientCommandSplitter& s, Buffer::iterator i)
        {
            s.last_command_is_bad = false;
            s._on_byte = [](ClientCommandSplitter&, byte) {};
            s._on_element = ClientCommandSplitter::base_type_on_element;
            s._call_base_on_element(i);
        }

        static void on_command_byte(ClientCommandSplitter& s, byte b)
        {
            s.last_command += std::toupper(b);
        }

        static void on_key_byte(ClientCommandSplitter& s, byte b)
        {
            s.slot_calc.next_byte(b);
        }

        static void special_parser_on_byte(ClientCommandSplitter& s, byte b)
        {
            s.special_parser->on_byte(b);
        }

        static void special_parser_on_element(ClientCommandSplitter& s, Buffer::iterator it)
        {
            s.special_parser->on_element(it);
            s._call_base_on_element(it);
        }

        static void base_type_on_element(ClientCommandSplitter& s, Buffer::iterator it)
        {
            s._call_base_on_element(it);
        }
    public:
        Buffer::iterator last_command_begin;

        std::string last_command;
        KeySlotCalc slot_calc;
        bool last_command_is_bad;

        void on_byte(byte b)
        {
            _on_byte(*this, b);
        }

        void on_element(Buffer::iterator i)
        {
            _on_element(*this, i);
        }

        util::sptr<SpecialCommandParser> special_parser;

        util::sref<Client> client;

        ClientCommandSplitter(Buffer::iterator i, util::sref<Client> cli)
            : BaseType(i)
            , _on_byte(ClientCommandSplitter::on_command_byte)
            , _on_element(ClientCommandSplitter::on_raw_element)
            , last_command_begin(i)
            , last_command_is_bad(false)
            , special_parser(nullptr)
            , client(cli)
        {}

        ClientCommandSplitter(ClientCommandSplitter&& rhs)
            : BaseType(std::move(rhs))
            , _on_byte(std::move(rhs._on_byte))
            , _on_element(std::move(rhs._on_element))
            , last_command_begin(rhs.last_command_begin)
            , last_command(std::move(rhs.last_command))
            , slot_calc(std::move(rhs.slot_calc))
            , last_command_is_bad(rhs.last_command_is_bad)
            , special_parser(std::move(rhs.special_parser))
            , client(rhs.client)
        {}

        bool handle_standard_key_command()
        {
            auto i = STD_COMMANDS.find(last_command);
            if (i == STD_COMMANDS.end()) {
                return false;
            }
            this->last_command_is_bad = true;
            this->_on_byte = ClientCommandSplitter::on_key_byte;
            this->_on_element = ClientCommandSplitter::on_command_key;
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
                this->_on_byte = ClientCommandSplitter::special_parser_on_byte;
                this->_on_element = ClientCommandSplitter::special_parser_on_element;
                return;
            }
            last_command_is_bad = true;
            this->_on_byte = [](ClientCommandSplitter&, byte) {};
            this->_on_element = ClientCommandSplitter::base_type_on_element;
        }

        void on_arr_end(Buffer::iterator i)
        {
            this->_on_byte = ClientCommandSplitter::on_command_byte;
            this->_on_element = ClientCommandSplitter::on_raw_element;
            if (last_command_is_bad) {
                client->push_command(util::mkptr(new DirectCommandGroup(
                    client, "-ERR Unknown command or command key not specified\r\n")));
            } else if (special_parser.nul()) {
                client->push_command(util::mkptr(new SingleCommandGroup(
                    client, Buffer(last_command_begin, i), slot_calc.get_slot())));
            } else {
                client->push_command(special_parser->spawn_commands(client, i));
                special_parser.reset();
            }
            last_command.clear();
            last_command_begin = i;
            slot_calc.reset();
            last_command_is_bad = false;
            BaseType::on_element(i);
        }

        void on_arr(cerb::rint size, Buffer::iterator i)
        {
            /*
             * Redis server will reset a request of more than 1M args.
             * See also
             * https://github.com/antirez/redis/blob/3.0/src/networking.c#L1014
             */
            if (size > 1024 * 1024) {
                throw BadRedisMessage("Request is too large");
            }
            if (!_nested_array_element_count.empty()) {
                throw BadRedisMessage("Invalid nested array as client command");
            }
            BaseType::on_arr(size, i);
            if (size == 0) {
                return;
            }
            this->_on_byte = ClientCommandSplitter::on_command_byte;
            this->_on_element = ClientCommandSplitter::on_command_arr_first_element;
        }
    };

}

void Command::on_remote_responsed(Buffer rsp, bool)
{
    this->buffer = std::move(rsp);
    this->responsed();
}

void Command::responsed()
{
    this->group->command_responsed();
}

void cerb::split_client_command(Buffer& buffer, util::sref<Client> cli)
{
    ClientCommandSplitter c(cerb::msg::split_by(
        buffer.begin(), buffer.end(), ClientCommandSplitter(
            buffer.begin(), cli)));
    if (c.finished()) {
        buffer.clear();
    } else {
        buffer.truncate_from_begin(c.interrupt_point());
    }
}

void Command::allow_write_commands()
{
    static std::set<std::string> const WRITE_COMMANDS({
        "EXPIRE", "EXPIREAT", "TTL", "PEXPIRE", "PEXPIREAT", "PERSIST", "RESTORE",

        "SET", "SETNX", "GETSET", "SETEX", "PSETEX", "SETBIT", "APPEND",
        "SETRANGE", "INCR", "DECR", "INCRBY", "DECRBY", "INCRBYFLOAT",

        "HSET", "HSETNX", "HDEL", "HINCRBY", "HINCRBYFLOAT", "HMSET",

        "LINSERT", "LPOP", "RPOP", "LPUSH", "LPUSHX",
        "RPUSH", "RPUSHX", "LREM", "LSET", "LTRIM", "SORT",

        "SADD", "SPOP", "SREM",

        "ZADD", "ZREM", "ZINCRBY", "ZREMRANGEBYLEX", "ZREMRANGEBYRANK", "ZREMRANGEBYSCORE",
    });
    for (std::string const& c: WRITE_COMMANDS) {
        STD_COMMANDS.insert(c);
    }
    static std::map<std::string, std::function<util::sptr<SpecialCommandParser>(
        Buffer::iterator, Buffer::iterator)>> const SPECIAL_WRITE_COMMAND(
    {
        {"DEL",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new DelCommandParser(arg_start));
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
        {"PUBLISH",
            [](Buffer::iterator command_begin, Buffer::iterator)
            {
                return util::mkptr(new PublishCommandParser(command_begin));
            }},
        {"KEYSINSLOT",
            [](Buffer::iterator, Buffer::iterator arg_start)
            {
                return util::mkptr(new KeysInSlotParser(arg_start));
            }},
    });
    for (auto const& c: SPECIAL_WRITE_COMMAND) {
        SPECIAL_RSP.insert(c);
    }
}
