#include "net.h"
#include "peer.h"
#include "http.h"
#include "http_headers.h"
#include <iostream>
#include <cstring>

using namespace std;

class MyHandler : public Net::Http::Handler {
    void onRequest(const Net::Http::Request& req, Net::Tcp::Peer& peer) {
        if (req.resource == "/ping") {
            if (req.method == Net::Http::Method::Get) {

                using namespace Net::Http;

                Net::Http::Response response(Net::Http::Code::Ok, "PONG");
               // response.headers
               //     .add(std::make_shared<Server>("lys"));
                response.writeTo(peer);

            }
        }
        else if (req.resource == "/echo") {
            if (req.method == Net::Http::Method::Post) {
                Net::Http::Response response(Net::Http::Code::Ok, req.body);

                response.writeTo(peer);
            }
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
    server.setHandler(std::make_shared<MyHandler>());

    server.serve();
}
