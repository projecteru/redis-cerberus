#include "event-loop-test.hpp"

int MultipleBuffersIO::close(int fd)
{
    if (this->poll_impl->has_pollee(fd)) {
        this->poll_impl->poll_del(0, fd);
    }
    return 0;
}

int AutomaticPoller::poll_wait(int, poll::pevent* events, int maxevents, int)
{
    return this->poll_wait(events, maxevents);
}

void AutomaticPoller::poll_add(int, int evtfd, void* data)
{
    ManualPoller::poll_add(0, evtfd, data);
    registered_data[evtfd] = data;
}

void AutomaticPoller::poll_add_read(int, int evtfd, void* data)
{
    ManualPoller::poll_add_read(0, evtfd, data);
    registered_data[evtfd] = data;
}

void AutomaticPoller::poll_del(int, int evtfd)
{
    ManualPoller::poll_del(0, evtfd);
    registered_data.erase(evtfd);
}

int AutomaticPoller::poll_wait(poll::pevent* events, int maxevents)
{
    int count = 0;
    for (auto i: this->pollees) {
        int flags = 0;
        if (this->event_is_write(i.second)) {
            flags = EV_WRITE;
        }
        if (!buffers->buffers[i.first].read_buffer.empty()) {
            flags |= EV_READ;
        }
        if (flags != 0) {
            events[count].events = flags;
            events[count].data.ptr = this->registered_data[i.first];
            ++count;
            if (count == maxevents) {
                return count;
            }
        }
    }
    return count;
}

util::sptr<cerb::Proxy> EventLoopTest::proxy(nullptr);
util::sptr<cerb::Acceptor> EventLoopTest::acceptor(nullptr);
util::sref<AutomaticPoller> EventLoopTest::poll_obj(nullptr);
util::sref<MultipleBuffersIO> EventLoopTest::io_obj(nullptr);

void EventLoopTest::SetUp()
{
    set_acceptor_fd_gen([]() {return EventLoopTest::io_obj->new_stream_socket();});
    EventLoopTest::proxy.reset(new cerb::Proxy);
    EventLoopTest::acceptor.reset(new cerb::Acceptor(*EventLoopTest::proxy, 0));

    util::sptr<AutomaticPoller> p(new AutomaticPoller);
    EventLoopTest::poll_obj = *p;
    PollNotImplement::set_impl(std::move(p));

    util::sptr<MultipleBuffersIO> poll_io(new MultipleBuffersIO(EventLoopTest::poll_obj));
    EventLoopTest::io_obj = *poll_io;
    EventLoopTest::poll_obj->buffers = EventLoopTest::io_obj;
    CIOImplement::set_impl(std::move(poll_io));
}

void EventLoopTest::TearDown()
{
    std::set<cerb::Connection*> conns;
    for (auto i: EventLoopTest::poll_obj->registered_data) {
        conns.insert(static_cast<cerb::Connection*>(i.second));
    }
    for (cerb::Connection* c: conns) {
        c->close();
    }
    for (cerb::Connection* c: conns) {
        c->after_events(conns);
    }

    EventLoopTest::io_obj.reset();
    CIOImplement::set_impl(util::mkptr(new CIOImplement));

    EventLoopTest::poll_obj.reset();
    PollNotImplement::set_impl(util::mkptr(new PollNotImplement));

    EventLoopTest::acceptor.reset(nullptr);
    EventLoopTest::proxy.reset(nullptr);
}
