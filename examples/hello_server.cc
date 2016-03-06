/* 
   Mathieu Stefani, 13 février 2016
   
   Example of an hello world server
*/


#include "endpoint.h"

using namespace Net;

class HelloHandler : public Http::Handler {
public:

    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& request, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "Hello World");
    }
};

int main() {
    Net::Address addr(Net::Ipv4::any(), Net::Port(9080));
    auto opts = Net::Http::Endpoint::options()
        .threads(1);

    Http::Endpoint server(addr);
    server.init(opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serve();

    server.shutdown();
}
