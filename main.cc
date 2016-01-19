#include "net.h"
#include "peer.h"
#include "http.h"
#include "http_headers.h"
#include "cookie.h"
#include "router.h"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace std;
using namespace Net;

struct ExceptionPrinter {
    void operator()(std::exception_ptr exc) const {
        try {
            std::rethrow_exception(exc);
        } catch (const std::exception& e) {
            std::cerr << "An exception occured: " << e.what() << std::endl;
        }
    }
};

void printCookies(const Net::Http::Request& req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto& c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

class MyHandler : public Net::Http::Handler {
    void onRequest(
            const Net::Http::Request& req,
            Net::Http::Response response,
            Net::Http::Timeout timeout) {

        if (req.resource() == "/ping") {
            if (req.method() == Net::Http::Method::Get) {

                using namespace Net::Http;

                timeout.arm(std::chrono::seconds(2));

                auto query = req.query();
                if (query.has("chunked")) {
                    std::cout << "Using chunked encoding" << std::endl;

                    response.headers()
                        .add<Header::Server>("lys")
                        .add<Header::ContentType>(MIME(Text, Plain));

                    response.cookies()
                        .add(Cookie("lang", "en-US"));

                    auto stream = response.stream(Net::Http::Code::Ok);
                    stream << "PO";
                    stream << "NG";
                    stream << ends;
                }

            }
        }
        else if (req.resource() == "/echo") {
            if (req.method() == Net::Http::Method::Post) {
                response.send(Net::Http::Code::Ok, req.body(), MIME(Text, Plain));
            }
        }
        else if (req.resource() == "/exception") {
            throw std::runtime_error("Exception thrown in the handler");
        }
        else if (req.resource() == "/timeout") {
            timeout.arm(std::chrono::seconds(5));
        }
#if 0
        else if (req.resource() == "/async") {
            std::thread([](Net::Http::Response response) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                response.send(Net::Http::Code::Ok, "Async response");
            }, std::move(response)).detach();
        }
#endif
        else if (req.resource() == "/static") {
            if (req.method() == Net::Http::Method::Get) {
                Net::Http::serveFile(response, "README.md").then([](ssize_t bytes) {;
                    std::cout << "Sent " << bytes << " bytes" << std::endl;
                }, Async::NoExcept);
            }
        }

    }

    void onTimeout(const Net::Http::Request& req, Net::Http::Response response) {
        response
            .send(Net::Http::Code::Request_Timeout, "Timeout")
            .then([=](ssize_t) { }, ExceptionPrinter());
    }
};

struct LoadMonitor {
    LoadMonitor(const std::shared_ptr<Net::Http::Endpoint>& endpoint)
        : endpoint_(endpoint)
        , interval(std::chrono::seconds(1))
    { }

    void setInterval(std::chrono::seconds secs) {
        interval = secs;
    }

    void start() {
        shutdown_ = false;
        thread.reset(new std::thread(std::bind(&LoadMonitor::run, this)));
    }

    void shutdown() {
        shutdown_ = true;
    }

    ~LoadMonitor() {
        shutdown_ = true;
        if (thread) thread->join();
    }

private:
    std::shared_ptr<Net::Http::Endpoint> endpoint_;
    std::unique_ptr<std::thread> thread;
    std::chrono::seconds interval;

    std::atomic<bool> shutdown_;

    void run() {
        Net::Tcp::Listener::Load old;
        while (!shutdown_) {
            if (!endpoint_->isBound()) continue;

            endpoint_->requestLoad(old).then([&](const Net::Tcp::Listener::Load& load) {
                old = load;

                double global = load.global;
                if (global > 100) global = 100;

                if (global > 1)
                    std::cout << "Global load is " << global << "%" << std::endl;
                else
                    std::cout << "Global load is 0%" << std::endl;
            },
            Async::NoExcept);

            std::this_thread::sleep_for(std::chrono::seconds(interval));
        }
    }
};

namespace Generic {

void handleReady(const Rest::Request&, Http::Response response) {
    response.send(Http::Code::Ok, "1");
}

}

class StatsEndpoint {
public:
    StatsEndpoint(Net::Address addr)
        : httpEndpoint(std::make_shared<Net::Http::Endpoint>(addr))
        , monitor(httpEndpoint)
    { }

    void init() {
        auto opts = Net::Http::Endpoint::options()
            .threads(1)
            .flags(Net::Tcp::Options::InstallSignalHandler);
        httpEndpoint->init(opts);
        setupRoutes();
        monitor.setInterval(std::chrono::seconds(5));
    }

    void start() {
        monitor.start();
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serve();
    }

    void shutdown() {
        httpEndpoint->shutdown();
        monitor.shutdown();
    }

private:
    void setupRoutes() {
        using namespace Net::Rest;

        Routes::Post(router, "/record/:name/:value?", Routes::bind(&StatsEndpoint::doRecordMetric, this));
        Routes::Get(router, "/value/:name", Routes::bind(&StatsEndpoint::doGetMetric, this));
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&StatsEndpoint::doAuth, this));

    }

    void doRecordMetric(const Rest::Request& request, Net::Http::Response response) {
        auto name = request.param(":name").as<std::string>();
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric& metric) {
            return metric.name() == name;
        });

        int val = 1;
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<int>();
        }

        if (it == std::end(metrics)) {
            metrics.push_back(Metric(std::move(name), val));
            response.send(Http::Code::Created, std::to_string(val));
        }
        else {
            auto &metric = *it;
            metric.incr(val);
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }

    }

    void doGetMetric(const Rest::Request& request, Net::Http::Response response) {
        auto name = request.param(":name").as<std::string>();
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric& metric) {
            return metric.name() == name;
        });

        if (it == std::end(metrics)) {
            response.send(Http::Code::Not_Found, "Metric does not exist");
        } else {
            const auto& metric = *it;
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }

    }

    void doAuth(const Rest::Request& request, Net::Http::Response response) {
        printCookies(request);
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        response.send(Http::Code::Ok);
    }

    class Metric {
    public:
        Metric(std::string name, int initialValue = 1)
            : name_(std::move(name))
            , value_(initialValue)
        { }

        int incr(int n = 1) {
            int old = value_;
            value_ += n;
            return old;
        }

        int value() const {
            return value_;
        }

        std::string name() const {
            return name_;
        }
    private:
        std::string name_;
        int value_;
    };

    std::vector<Metric> metrics;

    std::shared_ptr<Net::Http::Endpoint> httpEndpoint;
    Rest::Router router;
    LoadMonitor monitor;
};

int main(int argc, char *argv[]) {
    Net::Port port(9080);

    int thr = 2;

    if (argc >= 2) {
        port = std::stol(argv[1]);

        if (argc == 3)
            thr = std::stol(argv[2]);
    }

    Net::Address addr(Net::Ipv4::any(), port);
    static constexpr size_t Workers = 4;

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    StatsEndpoint stats(addr);

    stats.init();
    stats.start();

    std::cout << "Shutdowning server" << std::endl;
    stats.shutdown();
}
