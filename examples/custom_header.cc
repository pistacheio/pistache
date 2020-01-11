/*
   Mathieu Stefani, 15 février 2016

   Example of custom headers registering
*/

#include <pistache/net.h>
#include <pistache/http_headers.h>
#include <sys/types.h>

// Quiet a warning about "minor" and "major" being doubly defined.
#ifdef major
    #undef major
#endif
#ifdef minor
    #undef minor
#endif

using namespace Pistache;
using namespace Pistache::Http;

class XProtocolVersion : public Header::Header {
public:
    NAME("X-Protocol-Version");

    XProtocolVersion()
        : maj(0)
        , min(0)
    { }

    XProtocolVersion(uint32_t major, uint32_t minor)
        : maj(major)
        , min(minor)
    { }

    void parse(const std::string& str) override {
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

    void write(std::ostream& os) const override {
        os << maj;
        os << "." << min;
    }

    uint32_t majorVersion() const {
        return maj;
    }

    uint32_t minorVersion() const {
        return min;
    }

private:
    uint32_t maj;
    uint32_t min;
};

int main() {
    Header::Registry::instance().registerHeader<XProtocolVersion>();
}
