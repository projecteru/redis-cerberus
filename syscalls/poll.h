#ifndef __CERBERUS_SYSTEM_POLL_H__
#define __CERBERUS_SYSTEM_POLL_H__

#ifndef _USE_CANDIDATE_POLL_LIB

#include <sys/epoll.h>
#include <cerrno>

#include "except/exceptions.hpp"

namespace poll {

    int const MAX_EVENTS = 1024;

    typedef struct ::epoll_event pevent;

    inline bool event_is_hup(int events)
    {
        return (events & EPOLLRDHUP) != 0;
    }

    inline bool event_is_read(int events)
    {
        return (events & EPOLLIN) != 0;
    }

    inline bool event_is_write(int events)
    {
        return (events & EPOLLOUT) != 0;
    }

    inline int poll_create()
    {
        int fd = ::epoll_create(MAX_EVENTS);
        if (fd == -1) {
            throw cerb::SystemError("epoll_create", errno);
        }
        return fd;
    }

    inline int poll_wait(int epfd, pevent* events, int maxevents, int timeout)
    {
        int n = ::epoll_wait(epfd, events, maxevents, timeout);
        if (n == -1) {
            if (errno == EINTR) {
                return 0;
            }
            throw cerb::SystemError("epoll_wait", errno);
        }
        return n;
    }

    inline void poll_add_read(int epfd, int evtfd, void* data)
    {
        struct epoll_event ev;
        ev.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
        ev.data.ptr = data;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, evtfd, &ev) == -1) {
            throw cerb::SystemError("epoll_ctl add read", errno);
        }
    }

    inline void poll_add(int epfd, int evtfd, void* data)
    {
        struct epoll_event ev;
        ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
        ev.data.ptr = data;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, evtfd, &ev) == -1) {
            throw cerb::SystemError("epoll_ctl add", errno);
        }
    }

    inline void poll_read(int epfd, int evtfd, void* data)
    {
        struct epoll_event ev;
        ev.events = EPOLLET | EPOLLIN;
        ev.data.ptr = data;
        if (::epoll_ctl(epfd, EPOLL_CTL_MOD, evtfd, &ev) == -1) {
            throw cerb::SystemError("epoll_ctl modi", errno);
        }
    }

    inline void poll_write(int epfd, int evtfd, void* data)
    {
        struct epoll_event ev;
        ev.events = EPOLLET | EPOLLIN | EPOLLOUT;
        ev.data.ptr = data;
        if (::epoll_ctl(epfd, EPOLL_CTL_MOD, evtfd, &ev) == -1) {
            throw cerb::SystemError("epoll_ctl modio", errno);
        }
    }

    inline void poll_del(int epfd, int evtfd)
    {
        epoll_ctl(epfd, EPOLL_CTL_DEL, evtfd, NULL);
    }

}

#else /* _USE_CANDIDATE_POLL_LIB */

namespace poll {

    int const MAX_EVENTS = 1024;

    struct pevent {
        uint32_t events;
        union {
            void* ptr;
            int fd;
            uint32_t u32;
            uint64_t u64;
        } data;
    };

    bool event_is_hup(int events);
    bool event_is_read(int events);
    bool event_is_write(int events);

    int poll_create();
    int poll_wait(int epfd, pevent* events, int maxevents, int timeout);
    void poll_add(int epfd, int evtfd, void* data);
    void poll_add_read(int epfd, int evtfd, void* data);
    void poll_read(int epfd, int evtfd, void* data);
    void poll_write(int epfd, int evtfd, void* data);
    void poll_del(int epfd, int evtfd);

}

#endif /* _USE_CANDIDATE_POLL_LIB */

#endif /* __CERBERUS_SYSTEM_POLL_H__ */
