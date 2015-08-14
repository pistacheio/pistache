/* os.h
   Mathieu Stefani, 13 August 2015
   
   Operating system specific functions
*/

#pragma once

#include <chrono>
#include <vector>
#include "flags.h"
#include "common.h"

typedef int Fd;

int hardware_concurrency();
bool make_non_blocking(int fd);

namespace Polling {

enum class Mode {
    Level,
    Edge
};

enum class NotifyOn {
    Read,
    Write,
    Hangup
};

DECLARE_FLAGS_OPERATORS(NotifyOn);

struct Tag {
    friend class Epoll;

    explicit constexpr Tag(uint64_t value)
      : value_(value)
    { }

    constexpr uint64_t value() const { return value_; }

    friend constexpr bool operator==(Tag lhs, Tag rhs);

private:
    uint64_t value_;
};

inline constexpr bool operator==(Tag lhs, Tag rhs) {
    return lhs.value_ == rhs.value_;
}

struct Event {
    Event(Tag tag) :
        tag(tag)
    { }

    Flags<NotifyOn> flags;
    Fd fd;
    Tag tag;
};

class Epoll {
public:
    Epoll(size_t max = 128);

    void addFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode = Mode::Level);
    void addFdOneShot(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode = Mode::Level);

    void removeFd(Fd fd);
    void rearmFd(Fd fd, Flags<NotifyOn> interest, Tag tag, Mode mode = Mode::Level);

    int poll(std::vector<Event>& events,
             size_t maxEvents = Const::MaxEvents,
             std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) const;

private:
    int toEpollEvents(Flags<NotifyOn> interest) const;
    Flags<NotifyOn> toNotifyOn(int events) const;
    int epoll_fd;
};

} // namespace Polling
