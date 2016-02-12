/* 
   Mathieu Stefani, 07 février 2016
   
 * Http client example
*/

#include "net.h"
#include "http.h"
#include "client.h"
#include <atomic>

using namespace Net;
using namespace Net::Http;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: http_client host [page]" << std::endl;
        return 1;
    }

    std::string host = argv[1];
    std::string page;
    if (argc == 3)
       page = argv[2];
    else
        page = "/";

    Experimental::Client client(host);

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
