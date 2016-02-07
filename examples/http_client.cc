/* 
   Mathieu Stefani, 07 février 2016
   
 * Http client example
*/

#include "net.h"
#include "http.h"
#include "client.h"
#include <atomic>

using namespace Net;

int main() {
    Net::Http::Client client("http://supnetwork.org:9080");
    auto opts = Net::Http::Client::options()
        .threads(1)
        .maxConnections(20);

    using namespace Net::Http;

    constexpr size_t Requests = 1000;
    std::atomic<int> responsesReceived(0);

    client.init(opts);
    for (int i = 0; i < Requests; ++i) {
        client.get(client
                .request("/ping")
                .cookie(Cookie("FOO", "bar")))
        .then([&](const Http::Response& response) {
            responsesReceived.fetch_add(1);
            //std::cout << "code = " << response.code() << std::endl;
           // std::cout << "body = " << response.body() << std::endl;
        }, Async::NoExcept);
    }
    std::cout << "Sent " << Requests << " requests" << std::endl;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Received " << responsesReceived.load() << " responses" << std::endl;
    }
    client.shutdown();
}
