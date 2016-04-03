/* 
   Mathieu Stefani, 07 février 2016
   
 * Http client example
*/

#include "net.h"
#include "http.h"
#include "client.h"

using namespace Net;
using namespace Net::Http;

int main(int argc, char *argv[]) {
    if (argc < 1) {
        std::cerr << "Usage: http_client page" << std::endl;
        return 1;
    }

    std::string page = argv[1];

    Experimental::Client client;

    auto opts = Http::Experimental::Client::options()
        .threads(1)
        .maxConnections(64);
    client.init(opts);
    auto resp = client.get(page).cookie(Cookie("FOO", "bar")).send();

    resp.then([](Response response) {
        std::cout << "Response code = " << response.code() << std::endl;
        auto body = response.body();
        if (!body.empty())
            std::cout << "Response body = " << body << std::endl;
    }, Async::NoExcept);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    client.shutdown();
}
