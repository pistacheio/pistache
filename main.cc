#include "net.h"
#include "peer.h"
#include "http.h"
#include "http_headers.h"
#include <iostream>
#include <cstring>

using namespace std;

class MyHandler : public Net::Http::Handler {
    void onRequest(const Net::Http::Request& req, Net::Http::Response response) {
        if (req.resource() == "/ping") {
            if (req.method() == Net::Http::Method::Get) {

                using namespace Net::Http;

                response.headers()
                    .add(std::make_shared<Header::Server>("lys"))
                    .add(std::make_shared<Header::ContentType>(MIME(Text, Plain)));

                response.send(Net::Http::Code::Ok, "PONG");

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
    }
};

int main(int argc, char *argv[]) {
    Net::Port port(9080);

    if (argc == 2) {
        port = std::stol(argv[1]);
    }

    Net::Address addr(Net::Ipv4::any(), port);
    static constexpr size_t Workers = 4;

    cout << "Cores = " << hardware_concurrency() << endl;

    Net::Http::Endpoint server(addr);
    auto opts = Net::Http::Endpoint::options()
        .threads(2)
        .flags(Net::Tcp::Options::InstallSignalHandler);
    server.init(opts);
    server.setHandler(std::make_shared<MyHandler>());

    server.serve();
}
