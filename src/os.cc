/* os.cc
   Mathieu Stefani, 13 August 2015
   
*/

#include "os.h"
#include "common.h"
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <sys/epoll.h>

int hardware_concurrency() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo) {
        return std::count(std::istream_iterator<std::string>(cpuinfo),
                          std::istream_iterator<std::string>(),
                          std::string("processor"));
    }

    return sysconf(_SC_NPROCESSORS_ONLN);
}


bool make_non_blocking(int sfd)
{
    int flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1) return false; 

    flags |= O_NONBLOCK;
    int ret = fcntl (sfd, F_SETFL, flags);
    if (ret == -1) return false;

    return true;
}

namespace Polling {

    Epoll::Epoll(size_t max) {
       epoll_fd = TRY_RET(epoll_create(max));
    }

    void
    Epoll::addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
        struct epoll_event ev;
        ev.events = toEpollEvents(interest);
        if (mode == Mode::Edge)
            ev.events |= EPOLLET;
        ev.data.u64 = tag.value_;

        TRY(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
    }

    void
    Epoll::addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
        struct epoll_event ev;
        ev.events = toEpollEvents(interest);
        ev.events |= EPOLLONESHOT;
        if (mode == Mode::Edge)
            ev.events |= EPOLLET;
        ev.data.u64 = tag.value_;

        TRY(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev));
    }

    void
    Epoll::removeFd(Fd fd) {
        struct epoll_event ev;
        TRY(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev));
    }

    void
    Epoll::rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode) {
        struct epoll_event ev;
        ev.events = toEpollEvents(interest);
        if (mode == Mode::Edge)
            ev.events |= EPOLLET;
        ev.data.u64 = tag.value_;

        TRY(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev));
    }

    int
    Epoll::poll(std::vector<Event>& events, size_t maxEvents, std::chrono::milliseconds timeout) const {
        struct epoll_event evs[Const::MaxEvents];

        int ready_fds = epoll_wait(epoll_fd, evs, maxEvents, timeout.count());
        if (ready_fds > 0) {
            for (int i = 0; i < ready_fds; ++i) {
                const struct epoll_event *ev = evs + i;

                const Tag tag(ev->data.u64);

                Event event(tag);
                event.flags = toNotifyOn(ev->events);
                events.push_back(tag);
            }
        }

        return ready_fds;
    }

    int
    Epoll::toEpollEvents(Flags<NotifyOn> interest) const {
        int events;

        if (interest.hasFlag(NotifyOn::Read))
            events |= EPOLLIN;
        if (interest.hasFlag(NotifyOn::Write))
            events |= EPOLLOUT;
        if (interest.hasFlag(NotifyOn::Hangup))
            events |= EPOLLHUP;

        return events;
    }

    Flags<NotifyOn>
    Epoll::toNotifyOn(int events) const {
        Flags<NotifyOn> flags;

        if (events & EPOLLIN)
            flags.setFlag(NotifyOn::Read);
        if (events & EPOLLOUT)
            flags.setFlag(NotifyOn::Write);
        if (events & EPOLLHUP)
            flags.setFlag(NotifyOn::Hangup);

        return flags;
    }

} // namespace Poller
