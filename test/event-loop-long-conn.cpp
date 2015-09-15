#include "core/server.hpp"
#include "core/message.hpp"
#include "core/globals.hpp"
#include "event-loop-test.hpp"

using namespace cerb;
using cerb::msg::format_command;

typedef EventLoopTest EventLoopLongConnectionTest;

TEST_F(EventLoopLongConnectionTest, BlockedPops)
{
    Command::allow_write_commands();

    std::vector<RedisNode> nodes;
    RedisNode x(util::Address("10.0.0.1", 9000), "f473c7430eb413929229fa32c91cee391a908a4b");
    x.slot_ranges.insert(std::make_pair(0, 16383));
    RedisNode y(util::Address("10.0.0.1", 9001), "a34bf47213eb4a908a309223c742c991cee1399f");
    nodes.push_back(std::move(x));
    nodes.push_back(std::move(y));
    EventLoopTest::update_slots_map(nodes);

    Server* server9000 = EventLoopTest::proxy->get_server_by_slot(0);
    ASSERT_NE(nullptr, server9000);

    int client = EventLoopTest::connect_client();
    EventLoopTest::push_read_of(client, format_command("GET", {"h-893"}));
    EventLoopTest::run_all_polls();

    ASSERT_EQ(1, EventLoopTest::write_buffer_size(server9000->fd));
    ASSERT_EQ(format_command("GET", {"h-893"}), EventLoopTest::get_written_of(server9000->fd, 0));
    EventLoopTest::push_read_of(server9000->fd, "$-1\r\n");

    EventLoopTest::run_all_polls();
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$-1\r\n", EventLoopTest::get_written_of(client, 0));
    EventLoopTest::clear_buffer_of(client);
    EventLoopTest::clear_buffer_of(server9000->fd);

    EventLoopTest::push_read_of(client, format_command("BLPOP", {"h-893", "10"}));
    EventLoopTest::run_all_polls();

    int longconn = EventLoopTest::last_fd();
    ASSERT_NE(server9000->fd, longconn);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(longconn));
    ASSERT_EQ(format_command("BLPOP", {"h-893", "10"}), EventLoopTest::get_written_of(longconn, 0));

    EventLoopTest::push_read_of(longconn, "*2\r\n$5\r\nh-893\r\n$7\r\nnothing\r\n");
    EventLoopTest::run_all_polls();
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("*2\r\n$5\r\nh-893\r\n$7\r\nnothing\r\n", EventLoopTest::get_written_of(client, 0));
    EventLoopTest::clear_buffer_of(client);
    EventLoopTest::clear_buffer_of(longconn);

    EventLoopTest::push_read_of(client, format_command("GET", {"h-893"}));
    EventLoopTest::run_all_polls();
    EventLoopTest::push_read_of(server9000->fd, "$-1\r\n");
    EventLoopTest::run_all_polls();
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$-1\r\n", EventLoopTest::get_written_of(client, 0));
    EventLoopTest::clear_buffer_of(client);
    EventLoopTest::clear_buffer_of(server9000->fd);

    EventLoopTest::push_read_of(client, format_command("BRPOP", {"h-893", "10"}));
    EventLoopTest::run_all_polls();
    longconn = EventLoopTest::last_fd();
    ASSERT_NE(server9000->fd, longconn);
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(longconn));
    ASSERT_EQ(format_command("BRPOP", {"h-893", "10"}), EventLoopTest::get_written_of(longconn, 0));

    EventLoopTest::push_read_of(longconn, "-MOVED 0 10.0.0.1:9001\r\n");
    EventLoopTest::run_all_polls();
    int updater = EventLoopTest::last_fd();
    ASSERT_NE(longconn, updater);

    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$-1\r\n", EventLoopTest::get_written_of(client, 0));
    EventLoopTest::clear_buffer_of(client);
    EventLoopTest::clear_buffer_of(longconn);

    EventLoopTest::push_read_of(
        updater,
        "+a34bf47213eb4a908a309223c742c991cee1399f 10.0.0.1:9001"
        " master - 0 0 0 connected 0\n"
        "f473c7430eb413929229fa32c91cee391a908a4b 10.0.0.1:9000"
        " myself,master - 0 0 1 connected 1-16383\r\n");
    EventLoopTest::run_all_polls();

    ASSERT_FALSE(server9000->closed());
    Server* server9001 = EventLoopTest::proxy->get_server_by_slot(0);
    ASSERT_NE(server9000->fd, server9001->fd);

    EventLoopTest::push_read_of(client, format_command("BRPOP", {"h-893", "10"}));
    EventLoopTest::run_all_polls();
    longconn = EventLoopTest::last_fd();
    ASSERT_NE(server9001->fd, longconn);

    EventLoopTest::push_read_of(longconn, "$-1\r\n");
    EventLoopTest::run_all_polls();
    ASSERT_EQ(1, EventLoopTest::write_buffer_size(client));
    ASSERT_EQ("$-1\r\n", EventLoopTest::get_written_of(client, 0));
}
