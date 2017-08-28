/* 
   Mathieu Stefani, 15 février 2016
   
   Example of custom headers registering
*/

#include <pistache/net.h>
#include <pistache/http_headers.h>

using namespace Pistache;
using namespace Pistache::Http;

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
}
