/* io.h
   Mathieu Stefani, 05 novembre 2015
   
   I/O handling
*/

#pragma once

#include "flags.h"
#include "os.h"
#include "async.h"
#include "net.h"
#include "prototype.h"

#include <thread>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <sys/time.h>
#include <sys/resource.h>

namespace Io {

    class Service;
    class Message;

    class FdSet {
    public:
        friend class Service;

        struct Entry : private Polling::Event {
            Entry(Polling::Event&& event)
                : Polling::Event(std::move(event))
            { }

            bool isReadable() const {
                return flags.hasFlag(Polling::NotifyOn::Read);
            }
            bool isWritable() const {
                return flags.hasFlag(Polling::NotifyOn::Write);
            }
            bool isHangup() const {
                return flags.hasFlag(Polling::NotifyOn::Hangup);
            }

            Fd getFd() const { return this->fd; }
            Polling::Tag getTag() const { return this->tag; }
        };

        typedef std::vector<Entry>::iterator iterator;
        typedef std::vector<Entry>::const_iterator const_iterator;

        size_t size() const {
            return events_.size();
        }

        const Entry& at(size_t index) const {
            return events_.at(index);
        }

        const Entry& operator[](size_t index) const {
            return events_[index];
        }

        iterator begin() {
            return events_.begin();
        }

        iterator end() {
            return events_.end();
        }

        const_iterator begin() const {
            return events_.begin();
        }

        const_iterator end() const {
            return events_.end();
        }

    private:
        FdSet(std::vector<Polling::Event>&& events)
        {
            events_.reserve(events.size());
            for (auto &&event: events) {
                events_.push_back(std::move(event));
            }
        }

        std::vector<Entry> events_;
    };

    class Handler;

    class Service {
    public:
        void registerFd(Fd fd, Polling::NotifyOn interest, Polling::Mode mode = Polling::Mode::Level);
        void registerFdOneShot(Fd fd, Polling::NotifyOn interest, Polling::Mode mode = Polling::Mode::Level);
        void modifyFd(Fd fd, Polling::NotifyOn interest, Polling::Mode mode = Polling::Mode::Level);

        void registerFd(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode = Polling::Mode::Level);
        void registerFdOneShot(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode = Polling::Mode::Level);
        void modifyFd(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode = Polling::Mode::Level);

        void init(const std::shared_ptr<Handler>& handler);
        void run();
        void shutdown();

        std::thread::id thread() const { return thisId; }
        std::shared_ptr<Handler> handler() const { return handler_; }

    private:
        std::thread::id thisId;
        std::shared_ptr<Handler> handler_;

        std::atomic<bool> shutdown_;
        NotifyFd shutdownFd;

        Polling::Epoll poller;
    };

    class ServiceGroup {
    public:
        void init(
                size_t threads,
                const std::shared_ptr<Handler>& handler);

        void start();
        void shutdown();

        std::shared_ptr<Service> service(Fd fd) const;
        std::shared_ptr<Service> service(size_t index) const;
        std::vector<Async::Promise<rusage>> load() const;

        size_t size() const {
            return workers_.size();
        }

        bool empty() const {
            return workers_.empty();
        }

    private:
        class Worker {
        public:
            Worker();
            ~Worker();

            void init(const std::shared_ptr<Handler>& handler);

            Async::Promise<rusage> load();

            void run();
            void shutdown();
        
            std::shared_ptr<Service> service() const { return service_; }
        private:
            std::unique_ptr<std::thread> thread_;
            std::shared_ptr<Service> service_;
        }; 
        std::vector<std::unique_ptr<Worker>> workers_;
    };

    class Handler : public Prototype<Handler> {
    public:
        friend class Service;

        virtual void onReady(const FdSet& fds) = 0;
        virtual void registerPoller(Polling::Epoll& poller) { }

        virtual Async::Promise<rusage> load() {
            return Async::Promise<rusage>::rejected(std::runtime_error("Unimplemented"));
        }

        Service* io() const {
            return io_;
        }

    private:
        Service* io_;
    };

} // namespace Io
