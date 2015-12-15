#include "gtest/gtest.h"
#include "http.h"
#include <algorithm>
#include <string>

using namespace Net;
using namespace Net::Http;

class TestScenario {
public:

    TestScenario& recvData(const char* data) {
        buf += data;
        return *this;
    }

    TestScenario& exception() {
        entries.push_back(makeEntry<Exception>());
        return *this;
    }

    TestScenario& yield() {
        entries.push_back(makeEntry<Yield>());
        return *this;
    }

    TestScenario& method(Http::Method meth) {
        entries.push_back(makeEntry<Method>(meth));
        return *this;
    }

    TestScenario& resource(const std::string& res) {
        entries.push_back(makeEntry<Resource>(res));
        return *this;
    }

    TestScenario& version(Http::Version ver) {
        entries.push_back(makeEntry<Version>(ver));
        return *this;
    }

    TestScenario& crlf() {
        buf.append( {0xD, 0xA } );

        return *this;
    }

    void run() {
        for (const auto& entry: entries) {
            entry.element->feed(parser, entry.data);
            entry.element->check(parser);
        }
    }

private:
    struct Element {
        virtual void feed(Private::Parser& parser, const std::string& data) const {
            parser.feed(data.c_str(), data.size());
            parser.parse();
        }

        virtual void check(Private::Parser& parser) const = 0;
    };

    struct Entry {
        std::string data;
        std::unique_ptr<Element> element;
    };

    struct Exception : public Element {
        virtual void feed(Private::Parser& parser, const std::string& data) const {
            parser.feed(data.c_str(), data.size());
            ASSERT_THROW(parser.parse(), HttpError);
        }

        void check(Private::Parser& parser) const { }
    };

    struct Yield : public Element {
        void feed(Private::Parser& parser, const std::string& data) const {
            parser.feed(data.c_str(), data.size());
            ASSERT_EQ(parser.parse(), Private::State::Again);
        }

        void check(Private::Parser& parser) const { }
    };

    struct Method : public Element {
        Method(Http::Method method)
            : method_(method)
        { }

        void check(Private::Parser& parser) const {
            ASSERT_EQ(parser.request.method(), method_);
        }
    private:
        Http::Method method_;
    };

    struct Resource : public Element {
        Resource(const std::string& resource)
            : resource_(resource)
        { }

        void check(Private::Parser& parser) const {
            ASSERT_EQ(parser.request.resource(), resource_);
        }

    private:
        std::string resource_;
    };

    struct Version : public Element {
        Version(Http::Version version) :
            version_(version)
        { }

        void check(Private::Parser& parser) const {
            ASSERT_EQ(parser.request.version(), version_);
        }

    private:
        Http::Version version_;
    };

    template<typename E, typename... Args>
    Entry makeEntry(Args&& ...args) {
        Entry entry;

        entry.data = buf;
        entry.element.reset(new E(std::forward<Args>(args)...));
        buf.clear();

        return entry;
    }

    std::vector<Entry> entries;
    Private::Parser parser;
    std::string buf;
};

TEST(parser_test, test_http_parsing) {
    TestScenario scenario1;
    scenario1
        .recvData("GET").method(Method::Get)
        .recvData(" /foo ").resource("/foo")
        .recvData("HTTP/1.1").crlf().version(Version::Http11);

    scenario1.run();

    TestScenario scenario2;
    scenario2
        .recvData("GE").yield()
        .recvData("T").method(Method::Get)
        .recvData(" ").yield()
        .recvData("/foo").yield()
        .recvData("Index").yield()
        .recvData(" ").resource("/fooIndex")
        .recvData("HT").yield()
        .recvData("TP/").yield()
        .recvData("1.1").crlf().version(Version::Http11);

    scenario2.run();

    TestScenario scenario3;
    scenario3
        .recvData("POST").method(Method::Post)
        .recvData("-").exception();

    scenario3.run();

    TestScenario scenario4;
    scenario4
        .recvData("PUT").method(Method::Put)
        .recvData(" / ").resource("/")
        .recvData("H").yield()
        .recvData("T").yield()
        .recvData("T").yield()
        .recvData("P").yield()
        .recvData("/1.0").crlf().version(Version::Http10);

    scenario4.run();

}
