/* 
   Mathieu Stefani, 15 février 2016
   
   Example of custom headers registering
*/

#include "net.h"
#include "http_headers.h"
#include "client.h"

using namespace Net;
using namespace Net::Http;

class XProtocolVersion : public Header::Header {
public:
    NAME("X-Protocol-Version");

    XProtocolVersion()
        : min(0)
        , maj(0)
    { }

    XProtocolVersion(uint32_t major, uint32_t minor)
        : maj(major)
        , min(minor)
    { }

    void parse(const std::string& str) {
        auto p = str.find('.');
        std::string major, minor;
        if (p != std::string::npos) {
            major = str.substr(0, p);
            minor = str.substr(p + 1);
        }
        else {
            major = str;
        }

        maj = std::stoi(major);
        if (!minor.empty())
            min = std::stoi(minor);
    }

    void write(std::ostream& os) const {
        os << maj;
        os << "." << min;
    }

    uint32_t major() const {
        return maj;
    }

    uint32_t minor() const {
        return min;
    }

private:
    uint32_t min;
    uint32_t maj;
};

int main() {
    Header::Registry::registerHeader<XProtocolVersion>();

    Experimental::Client client("http://supnetwork.org:9080");
    
    auto ops = Experimental::Client::options()
        .threads(1)
        .maxConnections(64);
    client.init(ops);
    auto resp = client.get("/ping").header<XProtocolVersion>(1, 0).send();

    resp.then([](Response response) {
        std::cout << "Response code = " << response.code() << std::endl;
        auto body = response.body();
        if (!body.empty())
            std::cout << "Response body = " << body << std::endl;
    }, Async::NoExcept);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    client.shutdown();
}
