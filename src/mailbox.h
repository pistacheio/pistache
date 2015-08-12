/* mailbox.h
   Mathieu Stefani, 12 August 2015
   Copyright (c) 2014 Datacratic.  All rights reserved.
   
   A simple lock-free Mailbox implementation
*/

#pragma once

#include <atomic>
#include <stdexcept>

template<typename T>
class Mailbox {
public:
    const T *get() const {
        if (isEmpty()) {
            throw std::runtime_error("Can not retrieve mail from empty mailbox");
        }

        return data.load();
    }

    T *post(T *newData) {
        T *old = data.load();
        while (!data.compare_exchange_weak(old, newData))
        { }

        return old;
    }

    T *clear() {
        return data.exchange(nullptr);
    }

    bool isEmpty() const {
        return data == nullptr;
    }

private:
    std::atomic<T *> data;
};
