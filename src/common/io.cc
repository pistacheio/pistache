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
Service::registerFd(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode) {
    poller.addFd(fd, interest, tag, mode);
}

void
Service::registerFdOneShot(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode) {
    poller.addFdOneShot(fd, interest, tag, mode);
}

void
Service::modifyFd(Fd fd, Polling::NotifyOn interest, Polling::Tag tag, Polling::Mode mode) {
    poller.rearmFd(fd, interest, tag, mode);
}

void
Service::init(const std::shared_ptr<Handler>& handler) {
    handler_ = handler;
    handler_->io_ = this;

    handler_->registerPoller(poller);
    shutdown_.store(false);
}

void
Service::shutdown() {
    shutdown_.store(true);
    shutdownFd.notify();
}

void
Service::run() {
    if (!handler_)
        throw std::runtime_error("You need to set a handler before running an io service");

    shutdownFd.bind(poller);

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
            if (shutdown_) return;

            FdSet set(std::move(events));
            handler_->onReady(set);
        }
    }
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

std::shared_ptr<Service>
ServiceGroup::service(size_t index) const {
    if (index >= workers_.size())
        throw std::out_of_range("index out of range");

    return workers_[index]->service();
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

Async::Promise<rusage>
ServiceGroup::Worker::load() {
    return service_->handler()->load();
}

void
ServiceGroup::Worker::run() {
    thread_.reset(new std::thread([=]() {
        service_->run();
    }));
}

void
ServiceGroup::Worker::shutdown() {
    service_->shutdown();
}

}
