#include <gtest/gtest.h>

#include "utils/string.h"
#include "core/server.hpp"
#include "core/message.hpp"
#include "core/globals.hpp"
#include "event-loop-test.hpp"

using namespace cerb;
using cerb::msg::format_command;

typedef EventLoopTest EventLoopProxyDateTest;

TEST_F(EventLoopProxyDateTest, GetOnClusterDown)
{
    ASSERT_EQ(0, EventLoopTest::poll_obj->registered_data.size());
    int client_a = EventLoopTest::connect_client();
    ASSERT_EQ(1, EventLoopTest::poll_obj->registered_data.size());

    EventLoopTest::push_read_of(client_a, "+PING\r\n");
    int nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("+PONG\r\n", EventLoopTest::get_written_of(client_a, 0));
    EventLoopTest::clear_buffer_of(client_a);

    EventLoopTest::push_read_of(client_a, format_command("GET", {"x"}));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_TRUE(EventLoopTest::read_buffer_empty(client_a));
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("-CLUSTERDOWN The cluster is down\r\n", EventLoopTest::get_written_of(client_a, 0));
    EventLoopTest::clear_buffer_of(client_a);

    int client_b = EventLoopTest::connect_client();
    EventLoopTest::push_read_of(client_a, format_command("PING", {"x"}));
    EventLoopTest::push_read_of(client_a, format_command("GET", {"y"}));
    EventLoopTest::push_read_of(client_b, format_command("HGET", {"z", "u"}));
    EventLoopTest::push_read_of(client_b, format_command("GET", {"w"}));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    ASSERT_TRUE(EventLoopTest::read_buffer_empty(client_a));
    ASSERT_TRUE(EventLoopTest::read_buffer_empty(client_b));
    ASSERT_EQ(2, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("$1\r\nx\r\n", EventLoopTest::get_written_of(client_a, 0));
    ASSERT_EQ("-CLUSTERDOWN The cluster is down\r\n", EventLoopTest::get_written_of(client_a, 1));
    ASSERT_EQ("-CLUSTERDOWN The cluster is down\r\n", EventLoopTest::get_written_of(client_b, 0));
    ASSERT_EQ("-CLUSTERDOWN The cluster is down\r\n", EventLoopTest::get_written_of(client_b, 1));
}

TEST_F(EventLoopProxyDateTest, GeneralGet)
{
    std::vector<RedisNode> nodes;
    RedisNode x(util::Address("10.0.0.1", 8000), "34bf473c742c91cee391a908a30eb413929229fa");
    x.slot_ranges.insert(std::make_pair(0, 16383));
    nodes.push_back(std::move(x));
    EventLoopTest::proxy->notify_slot_map_updated(std::move(nodes));

    int client = EventLoopTest::connect_client();
    EventLoopTest::push_read_of(client, format_command("GET", {"hello"}));
    EventLoopTest::run_all_polls();

    Server* server = EventLoopTest::proxy->get_server_by_slot(0);
    ASSERT_NE(nullptr, server);
    ASSERT_FALSE(server->closed());

    EventLoopTest::push_read_of(server->fd, "$5\r\nworld\r\n");
    EventLoopTest::run_all_polls();
    EventLoopTest::push_read_of(client, format_command("MGET", {"hello", "Cerberus"}));
    EventLoopTest::run_all_polls();
    EventLoopTest::push_read_of(server->fd, "$5\r\nworld\r\n$-1\r\n");
    EventLoopTest::run_all_polls();

    ASSERT_EQ(4, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$5\r\nworld\r\n", EventLoopTest::get_written_of(client, 0));
    ASSERT_EQ("*2\r\n", EventLoopTest::get_written_of(client, 1));
    ASSERT_EQ("$5\r\nworld\r\n", EventLoopTest::get_written_of(client, 2));
    ASSERT_EQ("$-1\r\n", EventLoopTest::get_written_of(client, 3));
}

TEST_F(EventLoopProxyDateTest, GetSuccessOnManualSlotsUpdate)
{
    cerb_global::set_remotes({util::Address("10.0.0.1", 9000), util::Address("10.0.0.1", 9001)});
    int client_a = EventLoopTest::connect_client();
    ASSERT_EQ(3, EventLoopTest::poll_obj->registered_data.size());
    int updater = EventLoopTest::last_fd();
    ASSERT_NE(client_a, updater);

    EventLoopTest::push_read_of(client_a, format_command("GET", {"hello"}));
    EventLoopTest::push_read_of(
        updater,
        "+29fa34bf473c742c91cee391a908a30eb4139292 127.0.0.1:9005"
        " master - 0 0 0 connected 0-16383\r\n");
    int nfd = EventLoopTest::run_poll();
    ASSERT_EQ(3, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    int server = EventLoopTest::last_fd();
    ASSERT_NE(updater, server);
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(server));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server));
    ASSERT_EQ(format_command("GET", {"hello"}), EventLoopTest::get_written_of(server, 0));

    int client_b = EventLoopTest::connect_client();

    EventLoopTest::push_read_of(server, "$7\r\n0118999\r\n");
    EventLoopTest::push_read_of(client_b, format_command("HGET", {"set", "field"}));
    EventLoopTest::push_read_of(client_b, format_command("HGET", {"set", "spring"}));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);

    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_a));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("$7\r\n0118999\r\n", EventLoopTest::get_written_of(client_a, 0));

    EventLoopTest::push_read_of(server, "$4\r\nBart\r\n");
    EventLoopTest::push_read_of(server, "$7\r\nCzeslaw\r\n");
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_b));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(2, EventLoopTest::write_buffer_size(client_b));
    ASSERT_EQ("$4\r\nBart\r\n", EventLoopTest::get_written_of(client_b, 0));
    ASSERT_EQ("$7\r\nCzeslaw\r\n", EventLoopTest::get_written_of(client_b, 1));
}

TEST_F(EventLoopProxyDateTest, SlotsMoved)
{
    cerb_global::set_remotes({util::Address("10.0.0.1", 9000)});
    int client_a = EventLoopTest::connect_client();
    ASSERT_EQ(2, EventLoopTest::poll_obj->registered_data.size());
    int updater = EventLoopTest::last_fd();
    ASSERT_NE(client_a, updater);

    EventLoopTest::push_read_of(client_a, format_command("GET", {"hello"}));
    EventLoopTest::push_read_of(
        updater,
        "+29fa34bf473c742c91cee391a908a30eb4139292 10.0.0.1:9000"
        " master - 0 0 0 connected 0-16383\r\n");
    int nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    int server_a = EventLoopTest::last_fd();
    ASSERT_NE(updater, server_a);
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(server_a));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server_a));
    ASSERT_EQ(format_command("GET", {"hello"}), EventLoopTest::get_written_of(server_a, 0));

    int client_b = EventLoopTest::connect_client();

    EventLoopTest::push_read_of(server_a, "$7\r\n0118999\r\n");
    /* h-893 is in slot 0 */
    EventLoopTest::push_read_of(client_b, format_command("GET", {"h-893"}));
    /* 9eb8...7758 is in slot 16383 */
    EventLoopTest::push_read_of(client_b, format_command("GET", {"9eb8d277-d287-428b-8bc8-266ecdada9b8-incr-7758"}));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);

    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_a));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("$7\r\n0118999\r\n", EventLoopTest::get_written_of(client_a, 0));

    EventLoopTest::push_read_of(server_a, "-MOVED 0 10.0.0.1:9001\r\n");
    EventLoopTest::push_read_of(server_a, "$7\r\nCzeslaw\r\n");
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    updater = EventLoopTest::last_fd();
    EventLoopTest::push_read_of(
        updater,
        "+29fa34bf473c742c91cee391a908a30eb4139292 10.0.0.1:9001"
        " master - 0 0 0 connected 0-8191\n"
        "21952b372055dfdb5fa25b2761857831040472e1 10.0.0.1:9000"
        " myself,master - 0 0 1 connected 8192-16383\r\n");

    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_b));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    Server* s = EventLoopTest::proxy->get_server_by_slot(8192);
    ASSERT_NE(nullptr, s);
    ASSERT_FALSE(s->closed());
    ASSERT_EQ(server_a, s->fd);

    s = EventLoopTest::proxy->get_server_by_slot(0);
    ASSERT_NE(nullptr, s);
    ASSERT_FALSE(s->closed());
    ASSERT_NE(server_a, s->fd);

    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    int server_b = s->fd;
    EventLoopTest::push_read_of(server_b, "$4\r\nBart\r\n");
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    ASSERT_EQ(2, EventLoopTest::write_buffer_size(client_b));
    ASSERT_EQ("$4\r\nBart\r\n", EventLoopTest::get_written_of(client_b, 0));
    ASSERT_EQ("$7\r\nCzeslaw\r\n", EventLoopTest::get_written_of(client_b, 1));
}

TEST_F(EventLoopProxyDateTest, ClientResetWhenSlotsMoved)
{
    cerb_global::set_remotes({util::Address("10.0.0.1", 9000)});
    int client_a = EventLoopTest::connect_client();
    ASSERT_EQ(2, EventLoopTest::poll_obj->registered_data.size());
    int updater = EventLoopTest::last_fd();
    ASSERT_NE(client_a, updater);

    EventLoopTest::push_read_of(client_a, format_command("GET", {"hello"}));
    EventLoopTest::push_read_of(
        updater,
        "+29fa34bf473c742c91cee391a908a30eb4139292 10.0.0.1:9100"
        " master - 0 0 0 connected 0-16383\r\n");
    int nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    int server_a = EventLoopTest::last_fd();
    ASSERT_NE(updater, server_a);
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(server_a));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server_a));
    ASSERT_EQ(format_command("GET", {"hello"}), EventLoopTest::get_written_of(server_a, 0));

    int client_b = EventLoopTest::connect_client();

    EventLoopTest::push_read_of(server_a, "$7\r\n0118999\r\n");
    /* h-893 is in slot 0 */
    EventLoopTest::push_read_of(client_a, format_command("GET", {"h-893"}));
    /* 9eb8...5007 is in slot 1 */
    EventLoopTest::push_read_of(client_b, format_command("GET", {"9eb8d277-d287-428b-8bc8-266ecdada9b8-hash-5007"}));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(3, nfd);
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_a));
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(2, nfd);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("$7\r\n0118999\r\n", EventLoopTest::get_written_of(client_a, 0));
    EventLoopTest::clear_buffer_of(client_a);

    EventLoopTest::push_read_of(server_a, "-MOVED 0 10.0.0.1:9101\r\n");
    EventLoopTest::push_read_of(server_a, "-MOVED 0 10.0.0.1:9101\r\n");
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    updater = EventLoopTest::last_fd();
    EventLoopTest::push_read_of(
        updater,
        "+29fa34bf473c742c91cee391a908a30eb4139292 10.0.0.1:9101"
        " master - 0 0 0 connected 0-8191\n"
        "21952b372055dfdb5fa25b2761857831040472e1 10.0.0.1:9100"
        " myself,master - 0 0 1 connected 8192-16383\n\r\n");

    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_a));
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_b));

    EventLoopTest::reset_conn(client_b);

    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_b));
    ASSERT_TRUE(EventLoopTest::read_buffer_empty(client_b));

    Server* s = EventLoopTest::proxy->get_server_by_slot(8192);
    ASSERT_NE(nullptr, s);
    ASSERT_FALSE(s->closed());
    ASSERT_EQ(server_a, s->fd);

    s = EventLoopTest::proxy->get_server_by_slot(0);
    ASSERT_NE(nullptr, s);
    ASSERT_FALSE(s->closed());
    ASSERT_NE(server_a, s->fd);

    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    int server_b = s->fd;
    EventLoopTest::push_read_of(server_b, "$6\r\nDalvin\r\n");
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);

    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client_a));
    ASSERT_EQ("$6\r\nDalvin\r\n", EventLoopTest::get_written_of(client_a, 0));
    ASSERT_TRUE(EventLoopTest::write_buffer_empty(client_b));
    ASSERT_TRUE(EventLoopTest::read_buffer_empty(client_b));
}

TEST_F(EventLoopProxyDateTest, ServerResets)
{
    std::vector<RedisNode> nodes;
    RedisNode x(util::Address("10.0.0.2", 9000), "391a908a30eb413929229fa34bf473c742c91cee");
    x.slot_ranges.insert(std::make_pair(0, 8191));
    RedisNode y(util::Address("10.0.0.2", 9001), "42c991cee139213eb4a908a309229fa34bf473c7");
    y.slot_ranges.insert(std::make_pair(8192, 16383));
    nodes.push_back(std::move(x));
    nodes.push_back(std::move(y));
    EventLoopTest::proxy->notify_slot_map_updated(std::move(nodes));

    Server* server_a = EventLoopTest::proxy->get_server_by_slot(0);
    Server* server_b = EventLoopTest::proxy->get_server_by_slot(8192);
    ASSERT_NE(nullptr, server_a);
    ASSERT_NE(nullptr, server_b);
    ASSERT_NE(server_a->fd, server_b->fd);

    int client = EventLoopTest::connect_client();

    /* h-893 is in slot 0 */
    EventLoopTest::push_read_of(client, format_command("GET", {"h-893"}));
    /* 9eb8...7758 is in slot 16383 */
    EventLoopTest::push_read_of(client, format_command("GET", {"9eb8d277-d287-428b-8bc8-266ecdada9b8-incr-7758"}));
    EventLoopTest::run_all_polls();

    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server_a->fd));
    ASSERT_EQ(format_command("GET", {"h-893"}), EventLoopTest::get_written_of(server_a->fd, 0));
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server_b->fd));
    ASSERT_EQ(format_command("GET", {"9eb8d277-d287-428b-8bc8-266ecdada9b8-incr-7758"}),
              EventLoopTest::get_written_of(server_b->fd, 0));

    int last_fd = EventLoopTest::last_fd();
    EventLoopTest::reset_conn(server_a->fd);
    int updater = EventLoopTest::last_fd();
    ASSERT_NE(last_fd, updater);

    EventLoopTest::push_read_of(server_b->fd, "$6\r\nEmirin\r\n");
    EventLoopTest::run_all_polls();
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(updater));
    ASSERT_EQ(format_command("cluster", {"nodes"}), EventLoopTest::get_written_of(updater, 0));

    EventLoopTest::push_read_of(
        updater,
        "+42c991cee139213eb4a908a309229fa34bf473c7 10.0.0.2:9001"
        " master - 0 0 0 connected 0-16383\r\n");
    EventLoopTest::run_all_polls();

    ASSERT_TRUE(server_a->closed());
    ASSERT_FALSE(server_b->closed());
    EventLoopTest::push_read_of(server_b->fd, "$7\r\nGoliath\r\n");
    EventLoopTest::run_all_polls();

    ASSERT_EQ(2, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$7\r\nGoliath\r\n", EventLoopTest::get_written_of(client, 0));
    ASSERT_EQ("$6\r\nEmirin\r\n", EventLoopTest::get_written_of(client, 1));
}
