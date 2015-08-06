#include "utils/string.h"
#include "core/server.hpp"
#include "core/message.hpp"
#include "core/globals.hpp"
#include "event-loop-test.hpp"

using namespace cerb;
using cerb::msg::format_command;

typedef EventLoopTest EventLoopSlotMapUpdatingTest;

TEST_F(EventLoopSlotMapUpdatingTest, ServerReadAfterUpdateFailed)
{
    std::vector<RedisNode> nodes;
    RedisNode x(util::Address("10.0.0.1", 9001), "391a908a30eb413929229fa34bf473c742c91cef");
    x.slot_ranges.insert(std::make_pair(0, 0));
    RedisNode y(util::Address("10.0.0.1", 9000), "491a908a30eb413929229fa34bf473c742c91cd0");
    y.slot_ranges.insert(std::make_pair(1, 16383));
    nodes.push_back(std::move(x));
    nodes.push_back(std::move(y));
    EventLoopTest::proxy->notify_slot_map_updated(std::move(nodes));

    Server* server_a = EventLoopTest::proxy->get_server_by_slot(0);
    Server* server_b = EventLoopTest::proxy->get_server_by_slot(1);
    ASSERT_NE(nullptr, server_a);
    ASSERT_NE(nullptr, server_b);
    ASSERT_FALSE(server_a->closed());
    ASSERT_FALSE(server_b->closed());
    ASSERT_NE(server_a->fd, server_b->fd);

    int client = EventLoopTest::connect_client();
    EventLoopTest::push_read_of(client, format_command("INFO", {}));
    EventLoopTest::run_all_polls();

    /* a not in slot #0 */
    EventLoopTest::push_read_of(client, format_command("GET", {"a"}));
    int nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(std::set<int>({client}), EventLoopTest::last_pollees());
    nfd = EventLoopTest::run_poll();
    ASSERT_EQ(1, nfd);
    ASSERT_EQ(std::set<int>({server_b->fd}), EventLoopTest::last_pollees());

    EventLoopTest::reset_conn(server_a->fd);
    EventLoopTest::run_all_polls();

    int updater = EventLoopTest::last_fd();
    /* some slots not covered */
    std::set<int> updaters;
    for (int u = client + 1; u <= updater; ++u) {
        updaters.insert(u);
        EventLoopTest::push_read_of(
            u,
            "+391a908a30eb413929229fa34bf473c742c91cef 10.0.0.1:9000"
            " master - 0 0 0 connected 1-16383\n"
             "491a908a30eb413929229fa34bf473c742c91cd0 10.0.0.1:9001"
            " master - 0 0 0 connected"
            "\r\n");
    }
    EventLoopTest::push_read_of(server_b->fd, "$1\r\nb\r\n");
    std::set<int> fds(updaters);
    fds.insert(server_b->fd);
    poll::pevent events[poll::MAX_EVENTS];
    nfd = EventLoopTest::poll_obj->poll_wait(events, poll::MAX_EVENTS);
    ASSERT_EQ(fds, EventLoopTest::last_pollees());
    /* shift server_b to the last, so updaters close all servers before it reads */
    for (int i = 0; i < nfd - 1; ++i) {
        if (events[i].data.ptr == server_b) {
            std::swap(events[i].events, events[nfd - 1].events);
            std::swap(events[i].data.ptr, events[nfd - 1].data.ptr);
        }
    }
    EventLoopTest::proxy->handle_events(events, nfd);
}
