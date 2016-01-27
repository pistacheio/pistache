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
#include <unistd.h>


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
            TRY(write(event_fd, &val, sizeof val));
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

/*
 * An unbounded MPSC lock-free queue. Usefull for efficient cross-thread message passing.

 * push() and pop() are wait-free.
 * Might replace the Mailbox implementation below

 * Design comes from http://www.1024cores.net/home/lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue
*/
template<typename T>
class Queue {
public:
    struct Entry {
        friend class Queue;

        const T& data() const {
            return *reinterpret_cast<const T*>(&storage);
        }

        T& data() {
            return *reinterpret_cast<T*>(&storage);
        }

        ~Entry() {
            auto *d = reinterpret_cast<T *>(&storage);
            d->~T();
        }
    private:
        typedef typename std::aligned_storage<sizeof(T), alignof(T)>::type Storage;
        Storage storage;
        std::atomic<Entry *> next;
    };

    Queue()
    {
        auto *sentinel = new Entry;
        sentinel->next = nullptr;
        head.store(sentinel, std::memory_order_relaxed);
        tail = sentinel;
    }

    virtual ~Queue() {
        while (auto *e = pop()) delete e;
    }

    template<typename U>
    Entry* allocEntry(U&& u) const {
        auto *e = new Entry;
        new (&e->storage) T(std::forward<U>(u));
        return e;
    }

    void push(Entry *entry) {
        entry->next = nullptr;
        // @Note: we're using SC atomics here (exchange will issue a full fence),
        // but I don't think we should bother relaxing them for now
        auto *prev = head.exchange(entry);
        prev->next = entry;
    }

    Entry* pop() {
        auto *res = tail;
        auto *next = res->next.load(std::memory_order_acquire);
        if (next) {
            // Since it's Single-Consumer, the store does not need to be atomic
            tail = next;
            new (&res->storage) T(std::move(next->data()));
            return res;
        }
        return nullptr;
    }

private:
    std::atomic<Entry *> head;
    Entry *tail;
};

template<typename T>
class PollableQueue : public Queue<T>
{
public:
    typedef typename Queue<T>::Entry Entry;

    PollableQueue()
     : event_fd(-1) {
    }

    ~PollableQueue() {
        if (event_fd != -1) close(event_fd);
    }

    bool isBound() const {
        return event_fd != -1;
    }

    Polling::Tag bind(Polling::Epoll& poller) {
        using namespace Polling;

        if (isBound()) {
            throw std::runtime_error("The queue has already been bound");
        }

        event_fd = TRY_RET(eventfd(0, EFD_NONBLOCK));
        Tag tag(event_fd);
        poller.addFd(event_fd, NotifyOn::Read, tag);

        return tag;
    }

    void push(Entry* entry) {
        Queue<T>::push(entry);

        if (isBound()) {
            uint64_t val = 1;
            TRY(write(event_fd, &val, sizeof val));
        }
    }

    Entry *pop() {
        auto ret = Queue<T>::pop();

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
