/* timer_pool.h
   Mathieu Stefani, 09 février 2016
   
   A pool of timer fd to avoid creating fds everytime we need a timer and
   thus reduce the total number of system calls.

   Most operations are lock-free except resize operations needed when the
   pool is empty, in which case it's blocking but we expect it to be rare.
*/

#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

#include <unistd.h>

#include <pistache/os.h>
#include <pistache/reactor.h>

namespace Pistache {

namespace Default {
    static constexpr size_t InitialPoolSize = 128;
}

class TimerPool {
public:
    TimerPool(size_t initialSize = Default::InitialPoolSize);

    struct Entry {

        friend class TimerPool;

        Fd fd;

        Entry()
            : fd(-1)
            , registered(false)
        {
            state.store(static_cast<uint32_t>(State::Idle));
        }

        ~Entry() {
            if (fd != -1)
                close(fd);
        }

        void initialize();
        template<typename Duration>
        void arm(Duration duration) {
            if (fd == -1) return;

            armMs(std::chrono::duration_cast<std::chrono::milliseconds>(duration));
        }

        void disarm();

        void
        registerReactor(const Aio::Reactor::Key& key, Aio::Reactor* reactor) {
            if (!registered) {
                reactor->registerFd(key, fd, Polling::NotifyOn::Read);
                registered = true;
            }
        }

    private:
        void armMs(std::chrono::milliseconds value);
        enum class State : uint32_t { Idle, Used };
        std::atomic<uint32_t> state;

        bool registered;
    };

    std::shared_ptr<Entry> pickTimer();
    void releaseTimer(const std::shared_ptr<Entry>& timer);

private:
    std::vector<std::shared_ptr<Entry>> timers;
};

} // namespace Pistache
