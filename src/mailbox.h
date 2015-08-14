/* mailbox.h
   Mathieu Stefani, 12 August 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   A simple lock-free Mailbox implementation
*/

#pragma once
#include "common.h"
#include "os.h"

#include <atomic>
#include <stdexcept>
#include <sys/eventfd.h>

template<typename T>
class Mailbox {
public:
    Mailbox() {
        data.store(nullptr);
    }

    virtual ~Mailbox() { }

    const T *get() const {
        if (isEmpty()) {
            throw std::runtime_error("Can not retrieve mail from empty mailbox");
        }

        return data.load();
    }

    virtual T *post(T *newData) {
        T *old = data.load();
        while (!data.compare_exchange_weak(old, newData))
        { }

        return old;
    }

    virtual T *clear() {
        return data.exchange(nullptr);
    }

    bool isEmpty() const {
        return data == nullptr;
    }

private:
    std::atomic<T *> data;
};

template<typename T>
class PollableMailbox : public Mailbox<T>
{
public:
    PollableMailbox()
     : event_fd(-1) {
    }

    ~PollableMailbox() {
        if (event_fd != -1) close(event_fd);
    }

    bool isBound() const {
        return event_fd != -1;
    }

    Polling::Tag bind(Polling::Epoll& poller) {
        using namespace Polling;

        if (isBound()) {
            throw std::runtime_error("The mailbox has already been bound");
        }

        event_fd = TRY_RET(eventfd(0, EFD_NONBLOCK));
        Tag tag(event_fd);
        poller.addFd(event_fd, NotifyOn::Read, tag);

        return tag;
    }

    T *post(T *newData) {
        auto *ret = Mailbox<T>::post(newData);

        if (isBound()) {
            uint64_t val = 1;
            TRY_RET(write(event_fd, &val, sizeof val));
        }

        return ret;
    }

    T *clear() {
        auto ret = Mailbox<T>::clear();

        if (isBound()) {
            uint64_t val;
            for (;;) {
                ssize_t bytes = read(event_fd, &val, sizeof val);
                if (bytes == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else {
                        // TODO
                    }
                }
            }
        }

        return ret;

    }

    Polling::Tag tag() const {
        if (!isBound())
            throw std::runtime_error("Can not retrieve tag of an unbound mailbox");

        return Polling::Tag(event_fd);
    }

    void unbind(Polling::Epoll& poller) {
        if (event_fd == -1) {
            throw std::runtime_error("The mailbox is not bound");
        }

        poller.removeFd(event_fd);
        close(event_fd), event_fd = -1;
    }

private:
    int event_fd;
};
