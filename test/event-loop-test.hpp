#ifndef __CERBERUS_TEST_EVENT_LOOP_TEST_HPP__
#define __CERBERUS_TEST_EVENT_LOOP_TEST_HPP__

#include "mock-poll.hpp"
#include "mock-acceptor.hpp"
#include "core/proxy.hpp"

struct MultipleBuffersIO
    : CIOImplement
{
    explicit MultipleBuffersIO(util::sref<ManualPoller> poll)
        : last_fd(0)
        , poll_impl(poll)
    {}

    std::map<int, BufferIO> buffers;
    int last_fd;
    util::sref<ManualPoller> poll_impl;

    ssize_t read(int fd, void* b, size_t count)
    {
        EXPECT_NE(-1, fd);
        return buffers[fd].read(0, b, count);
    }

    ssize_t write(int fd, void const* buf, size_t count)
    {
        EXPECT_NE(-1, fd);
        return buffers[fd].write(0, buf, count);
    }

    int close(int fd);

    int new_stream_socket()
    {
        return ++this->last_fd;
    }

    void push_writing_size(int fd, int sz)
    {
        buffers[fd].writing_sizes.push_back(sz);
    }
};

struct AutomaticPoller
    : ManualPoller
{
    util::sref<MultipleBuffersIO> buffers;

    AutomaticPoller()
        : buffers(nullptr)
    {}

    int poll_create() {return 0;}
    int poll_wait(int, poll::pevent* events, int maxevents, int);
    void poll_add_read(int, int evtfd, void* data);
    void poll_add_write(int, int evtfd, void* data);
    void poll_del(int, int evtfd);

    std::map<int, void*> registered_data;
    std::set<int> last_pollees;

    int poll_wait(poll::pevent events[], int maxevents);
};

struct EventLoopTest
    : testing::Test
{
    static util::sptr<cerb::Proxy> proxy;
    static util::sptr<cerb::Acceptor> acceptor;
    static util::sref<AutomaticPoller> poll_obj;
    static util::sref<MultipleBuffersIO> io_obj;

    static bool read_buffer_empty(int fd)
    {
        return EventLoopTest::io_obj->buffers[fd].read_buffer.empty();
    }

    static bool write_buffer_empty(int fd)
    {
        return EventLoopTest::io_obj->buffers[fd].write_buffer.empty();
    }

    static size_t read_buffer_size(int fd)
    {
        return EventLoopTest::io_obj->buffers[fd].read_buffer.size();
    }

    static size_t write_buffer_size(int fd)
    {
        return EventLoopTest::io_obj->buffers[fd].write_buffer.size();
    }

    static void push_read_of(int fd, std::string cont)
    {
        return EventLoopTest::io_obj->buffers[fd].read_buffer.push_back(std::move(cont));
    }

    static std::string const& get_written_of(int fd, size_t index)
    {
        return EventLoopTest::io_obj->buffers[fd].write_buffer[index];
    }

    static void clear_buffer_of(int fd)
    {
        EventLoopTest::io_obj->buffers[fd].clear();
    }

    static int last_fd()
    {
        return EventLoopTest::io_obj->last_fd;
    }

    static int connect_client()
    {
        poll::pevent ev;
        ev.events = 0;
        ev.data.ptr = get_acceptor();
        EventLoopTest::proxy->handle_events(&ev, 1);
        return ::last_client_fd();
    }

    static void reset_conn(int fd)
    {
        poll::pevent ev;
        ev.events = ManualPoller::EV_HUP;
        ev.data.ptr = EventLoopTest::poll_obj->registered_data[fd];
        EventLoopTest::proxy->handle_events(&ev, 1);
        EventLoopTest::poll_obj->last_pollees = {fd};
    }

    static int run_poll()
    {
        poll::pevent events[poll::MAX_EVENTS];
        int nfd = EventLoopTest::poll_obj->poll_wait(events, poll::MAX_EVENTS);
        EventLoopTest::proxy->handle_events(events, nfd);
        return nfd;
    }

    static std::set<int> const& last_pollees()
    {
        return EventLoopTest::poll_obj->last_pollees;
    }

    static void run_all_polls()
    {
        while (0 != EventLoopTest::run_poll())
            ;
    }

    static void update_slots_map(std::vector<cerb::RedisNode> const& nodes,
                                 cerb::msize_t covered_slots=cerb::CLUSTER_SLOT_COUNT)
    {
        std::set<util::Address> remotes;
        for (auto const& n: nodes) {
            remotes.insert(n.addr);
        }
        EventLoopTest::proxy->notify_slot_map_updated(nodes, remotes, covered_slots);
    }

    void SetUp();
    void TearDown();
};

#endif /* __CERBERUS_TEST_EVENT_LOOP_TEST_HPP__ */
