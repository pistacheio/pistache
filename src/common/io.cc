/* io.cc
   Mathieu Stefani, 05 novembre 2015
   
   I/O handling
*/

#include <thread>
#include "io.h"
#include "peer.h"
#include "os.h"
#include <sys/timerfd.h>

namespace Io {

struct Message {
    virtual ~Message() { }

    enum class Type { Shutdown };

    virtual Type type() const = 0;
};

struct ShutdownMessage : public Message {
    Type type() const { return Type::Shutdown; }
};

template<typename To>
To *message_cast(const std::unique_ptr<Message>& from)
{
    return static_cast<To *>(from.get());
}

Service::Service()
{
    notifier.bind(poller);
}

void
Service::registerFd(Fd fd, Polling::NotifyOn interest, Polling::Mode mode) {
    poller.addFd(fd, interest, Polling::Tag(fd), mode);
}

void
Service::registerFdOneShot(Fd fd, Polling::NotifyOn interest, Polling::Mode mode) {
    poller.addFdOneShot(fd, interest, Polling::Tag(fd), mode);
}

void
Service::modifyFd(Fd fd, Polling::NotifyOn interest, Polling::Mode mode) {
    poller.rearmFd(fd, interest, Polling::Tag(fd), mode);
}

void
Service::init(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
    handler_->io_ = this;

    handler_->registerPoller(poller);
}

void
Service::shutdown() {
    mailbox.post(new ShutdownMessage());
}

void
Service::run() {
    if (!handler_)
        throw std::runtime_error("You need to set a handler before running an io service");

    timerFd = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
    poller.addFd(timerFd, Polling::NotifyOn::Read, Polling::Tag(timerFd));

    mailbox.bind(poller);

    thisId = std::this_thread::get_id();

    std::chrono::milliseconds timeout(-1);

    for (;;) {
        std::vector<Polling::Event> events;

        int ready_fds;
        switch(ready_fds = poller.poll(events, 1024, timeout)) {
        case -1:
            break;
        case 0:
            timeout = std::chrono::milliseconds(-1);
            break;
        default:
            std::vector<Polling::Event> evs;
            for (auto& event: events) {
                if (event.tag == mailbox.tag()) {
                    std::unique_ptr<Message> msg(mailbox.clear());
                    if (msg->type() == Message::Type::Shutdown) {
                        return;
                    }
                }
                else if (event.tag == notifier.tag()) {
                    handleNotify();
                }
                else {
                    if (event.flags.hasFlag(Polling::NotifyOn::Read)) {
                        auto fd = event.tag.value();
                        if (fd == timerFd) {
                            handleTimeout();
                            continue;
                        }
                    }
                    evs.push_back(std::move(event));
                }
            }

            FdSet set(std::move(evs));
            handler_->onReady(set);
        }
    }
}

void
Service::armTimerMs(
        std::chrono::milliseconds value,
        Async::Resolver resolve, Async::Rejection reject)
{
    itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;

    if (value.count() < 1000) {
        spec.it_value.tv_sec = 0;
        spec.it_value.tv_nsec
            = std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
    } else {
        spec.it_value.tv_sec
            = std::chrono::duration_cast<std::chrono::seconds>(value).count();
        spec.it_value.tv_nsec = 0;
    }


    int res = timerfd_settime(timerFd, 0, &spec, 0);
    if (res == -1) {
        reject(Net::Error::system("Could not set timer time"));
        return;
    }

    timer = Some(Timer(value, std::move(resolve), std::move(reject)));

}

void
Service::disarmTimer()
{
    if (!timer.isEmpty()) {
        itimerspec spec;
        spec.it_value.tv_sec = spec.it_value.tv_nsec = 0;
        spec.it_interval.tv_sec = spec.it_interval.tv_nsec = 0;

        int res = timerfd_settime(timerFd, 0, &spec, 0);

        if (res == -1)
            throw Net::Error::system("Could not set timer time");

        timer = None();
    }
}

void
Service::handleNotify() {
    optionally_do(load_, [&](const Async::Holder& async) {
        while (this->notifier.tryRead()) ;

        rusage now;

        auto res = getrusage(RUSAGE_THREAD, &now);
        if (res == -1)
            async.reject(std::runtime_error("Could not compute usage"));

        async.resolve(now);
    });

    load_ = None();
}

void
Service::handleTimeout() {

    optionally_do(timer, [=](const Timer& entry) {
        uint64_t numWakeups;
        int res = ::read(timerFd, &numWakeups, sizeof numWakeups);
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            else
                entry.reject(Net::Error::system("Could not read timerfd"));
        } else {
            if (res != sizeof(numWakeups)) {
                entry.reject(Net::Error("Read invalid number of bytes for timer fd: "
                            + std::to_string(timerFd)));
            }
            else {
                entry.resolve(numWakeups);
            }
        }
    });
}

void
ServiceGroup::init(
        size_t threads,
        const std::shared_ptr<Handler>& handler) {
    for (size_t i = 0; i < threads; ++i) {
        std::unique_ptr<Worker> wrk(new Worker);
        wrk->init(handler->clone());
        workers_.push_back(std::move(wrk));
    }
}

void
ServiceGroup::start() {
    for (auto& worker: workers_) {
        worker->run();
    }
}

std::vector<Async::Promise<rusage>>
ServiceGroup::load() const {
    std::vector<Async::Promise<rusage>> loads;
    loads.reserve(workers_.size());

    for (auto& worker: workers_) {
        loads.push_back(worker->load());
    }

    return loads;
}


void
ServiceGroup::shutdown() {
    for (auto& worker: workers_)
        worker->shutdown();
}

std::shared_ptr<Service>
ServiceGroup::service(Fd fd) const {
    size_t worker = fd % workers_.size();
    auto& wrk = workers_[worker];
    return wrk->service();
}
    
ServiceGroup::Worker::Worker() {
    service_.reset(new Service);
}

ServiceGroup::Worker::~Worker() {
    if (thread_) thread_->join();
}

void
ServiceGroup::Worker::init(const std::shared_ptr<Handler>& handler) {
    service_->init(handler);
}

void
ServiceGroup::Worker::run() {
    thread_.reset(new std::thread([=]() {
        service_->run();
    }));
}

void
ServiceGroup::Worker::shutdown() {
    service_->mailbox.post(new ShutdownMessage);
}

}
