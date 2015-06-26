#ifndef __CERBERUS_TEST_MOCK_POLL_HPP__
#define __CERBERUS_TEST_MOCK_POLL_HPP__

#include <map>

#include "utils/pointer.h"
#include "syscalls/poll.h"
#include "syscalls/cio.h"
#include "mock-io.hpp"

class PollNotImplement {
    static util::sptr<PollNotImplement> _p;
public:
    virtual ~PollNotImplement() {}

    static void set_impl(util::sptr<PollNotImplement> p);
    static util::sref<PollNotImplement> get_impl();

    virtual bool event_is_hup(int events);
    virtual bool event_is_read(int events);
    virtual bool event_is_write(int events);

    virtual int poll_create();
    virtual int poll_wait(int epfd, poll::pevent* events, int maxevents, int timeout);
    virtual void poll_add(int epfd, int evtfd, void* data);
    virtual void poll_add_read(int epfd, int evtfd, void* data);
    virtual void poll_read(int epfd, int evtfd, void* data);
    virtual void poll_write(int epfd, int evtfd, void* data);
    virtual void poll_del(int epfd, int evtfd);
};

struct ManualPoller
    : PollNotImplement
{
    static int const EV_HUP = 1;
    static int const EV_READ = 2;
    static int const EV_WRITE = 4;

    bool event_is_hup(int events);
    bool event_is_read(int events);
    bool event_is_write(int events);

    void poll_add(int, int evtfd, void* data);
    void poll_add_read(int, int evtfd, void* data);
    void poll_read(int, int evtfd, void* data);
    void poll_write(int, int evtfd, void* data);
    void poll_del(int, int evtfd);

    std::map<int, int> pollees;

    bool has_pollee(int evtfd) const;
    int get_pollee_events(int evtfd) const;
    void clear_pollee_events(int fd);
};

struct PollerBufferIO
    : BufferIO
{
    util::sref<ManualPoller> poll_impl;

    explicit PollerBufferIO(util::sref<ManualPoller> p)
        : poll_impl(p)
    {}

    int close(int fd);
};

#endif /* __CERBERUS_TEST_MOCK_POLL_HPP__ */
