#include <gtest/gtest.h>

#include "mock-poll.hpp"

void PollNotImplement::set_impl(util::sptr<PollNotImplement> p)
{
    _p = std::move(p);
}

util::sref<PollNotImplement> PollNotImplement::get_impl()
{
    return *_p;
}

util::sptr<PollNotImplement> PollNotImplement::_p(new PollNotImplement);

bool PollNotImplement::event_is_hup(int) { return false; }
bool PollNotImplement::event_is_read(int) { return false; }
bool PollNotImplement::event_is_write(int) { return false; }

int PollNotImplement::poll_create() { return 0; }
int PollNotImplement::poll_wait(int, poll::pevent*, int, int) { return 0; }
void PollNotImplement::poll_add(int, int, void*) {}
void PollNotImplement::poll_add_read(int, int, void*) {}
void PollNotImplement::poll_read(int, int, void*) {}
void PollNotImplement::poll_write(int, int, void*) {}
void PollNotImplement::poll_del(int, int) {}

bool poll::event_is_hup(int events)
{
    return PollNotImplement::get_impl()->event_is_hup(events);
}

bool poll::event_is_read(int events)
{
    return PollNotImplement::get_impl()->event_is_read(events);
}

bool poll::event_is_write(int events)
{
    return PollNotImplement::get_impl()->event_is_write(events);
}

int poll::poll_create()
{
    return PollNotImplement::get_impl()->poll_create();
}

int poll::poll_wait(int epfd, poll::pevent* events, int maxevents, int timeout)
{
    return PollNotImplement::get_impl()->poll_wait(epfd, events, maxevents, timeout);
}

void poll::poll_add(int epfd, int evtfd, void* data)
{
    return PollNotImplement::get_impl()->poll_add(epfd, evtfd, data);
}

void poll::poll_add_read(int epfd, int evtfd, void* data)
{
    return PollNotImplement::get_impl()->poll_add_read(epfd, evtfd, data);
}

void poll::poll_read(int epfd, int evtfd, void* data)
{
    return PollNotImplement::get_impl()->poll_read(epfd, evtfd, data);
}

void poll::poll_write(int epfd, int evtfd, void* data)
{
    return PollNotImplement::get_impl()->poll_write(epfd, evtfd, data);
}

void poll::poll_del(int epfd, int evtfd)
{
    return PollNotImplement::get_impl()->poll_del(epfd, evtfd);
}

bool ManualPoller::event_is_hup(int events)
{
    return (events & EV_HUP) != 0;
}

bool ManualPoller::event_is_read(int events)
{
    return (events & EV_READ) != 0;
}

bool ManualPoller::event_is_write(int events)
{
    return (events & EV_WRITE) != 0;
}

void ManualPoller::poll_add(int, int evtfd, void* data)
{
    EXPECT_FALSE(this->has_pollee(evtfd)) << evtfd << " in " << data;
    pollees[evtfd] = EV_HUP | EV_READ | EV_WRITE;
}

void ManualPoller::poll_add_read(int, int evtfd, void* data)
{
    EXPECT_FALSE(this->has_pollee(evtfd)) << evtfd << " in " << data;
    pollees[evtfd] = EV_HUP | EV_READ;
}

void ManualPoller::poll_read(int, int evtfd, void* data)
{
    EXPECT_TRUE(this->has_pollee(evtfd)) << evtfd << " in " << data;
    pollees[evtfd] = EV_HUP | EV_READ;
}

void ManualPoller::poll_write(int, int evtfd, void* data)
{
    EXPECT_TRUE(this->has_pollee(evtfd)) << evtfd << " in " << data;
    pollees[evtfd] = EV_HUP | EV_READ | EV_WRITE;
}

void ManualPoller::poll_del(int, int evtfd)
{
    EXPECT_TRUE(this->has_pollee(evtfd)) << evtfd;
    pollees.erase(evtfd);
}

bool ManualPoller::has_pollee(int evtfd) const
{
    return this->pollees.find(evtfd) != this->pollees.end();
}

int ManualPoller::get_pollee_events(int evtfd) const
{
    auto f = this->pollees.find(evtfd);
    if (f == this->pollees.end()) {
        return 0;
    }
    return f->second;
}

void ManualPoller::clear_pollee_events(int evtfd)
{
    EXPECT_TRUE(this->has_pollee(evtfd));
    pollees[evtfd] = 0;
}

int PollerBufferIO::close(int fd)
{
    if (this->poll_impl->has_pollee(fd)) {
        this->poll_impl->poll_del(0, fd);
    }
    return 0;
}
