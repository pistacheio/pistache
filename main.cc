#include "net.h"
#include "peer.h"
#include "http.h"
#include "http_headers.h"
#include <iostream>
#include <cstring>

using namespace std;

struct ExceptionPrinter {
    void operator()(std::exception_ptr exc) const {
        try {
            std::rethrow_exception(exc);
        } catch (const std::exception& e) {
            std::cerr << "An exception occured: " << e.what() << std::endl;
        }
    }
};

class MyHandler : public Net::Http::Handler {
    void onRequest(
            const Net::Http::Request& req,
            Net::Http::Response response,
            Net::Http::Timeout timeout) {

        if (req.resource() == "/ping") {
            if (req.method() == Net::Http::Method::Get) {

                using namespace Net::Http;

                response.headers()
                    .add<Header::Server>("lys")
                    .add<Header::ContentType>(MIME(Text, Plain));

#if 0
                auto stream = response.stream(Net::Http::Code::Ok);
                stream << "PO";
                stream << flush;
                stream << "NG";
                stream << ends;
#endif

                response.send(Code::Ok, "PONG");
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
    }

    void onTimeout(const Net::Http::Request& req, Net::Http::Response response) {
        response
            .send(Net::Http::Code::Request_Timeout, "Timeout")
            .then([=](ssize_t) { }, ExceptionPrinter());
    }
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

    Net::Http::Endpoint server(addr);
    auto opts = Net::Http::Endpoint::options()
        .threads(thr)
        .flags(Net::Tcp::Options::InstallSignalHandler);
    server.init(opts);
    server.setHandler(std::make_shared<MyHandler>());

    server.serve();
}
