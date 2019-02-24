#include "gtest/gtest.h"
#include <pistache/http.h>
#include <pistache/stream.h>

using namespace Pistache;

// @Todo: Add an easy to use fixture to inject data for parsing tests.

TEST(http_parsing_test, should_parse_http_request_in_two_packets_issue_160)
{
    Http::Private::Parser<Http::Request> parser;

    auto feed = [&parser](const char* data)
    {
        parser.feed(data, std::strlen(data));
    };

    // First, we feed the parser with a Request-Line
    feed("GET /hello HTTP/1.1\r\n");
    ASSERT_EQ(parser.parse(), Http::Private::State::Again);
    // @Todo @Completeness We should also assert that we are in the correct step. However, the step is currently not
    // exposed by the parser. Since the parser is supposed to stay "private", we could either directly expose the step
    // or return it from the parse() method.

    // Let's now put some headers
    feed("User-Agent: Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2228.0 Safari/537.36\r\n");
    feed("Host: localhost\r\n");
    feed("Content-Length: 5\r\n");
    feed("\r\n");
    ASSERT_EQ(parser.parse(), Http::Private::State::Again);

    // Finally, we finish the body
    feed("HELLO");
    ASSERT_EQ(parser.parse(), Http::Private::State::Done);
}

TEST(http_parsing_test, succ_response_line_step)
{
    Http::Response response;
    Http::Private::ResponseLineStep step(&response);

    std::string line("HTTP/1.1 200 OK\r\n");
    RawStreamBuf<> buf(&line[0], line.size());
    StreamCursor cursor(&buf);

    Http::Private::State state = step.apply(cursor);

    ASSERT_EQ(state, Http::Private::State::Next);
    ASSERT_EQ(response.code(), Http::Code::Ok);
}

TEST(http_parsing_test, error_response_line_step)
{
    std::vector<std::string> lines = {"HTTP/ABC.DEF 200 OK\r\n",
                                      "HTTP/1.1200 OK\r\n",
                                      "HTTP/ABC.DEF 200\r\n"};
    for (auto& line: lines)
    {
        Http::Response response;
        Http::Private::ResponseLineStep step(&response);

        RawStreamBuf<> buf(&line[0], line.size());
        StreamCursor cursor(&buf);

        ASSERT_THROW(step.apply(cursor), Http::HttpError);
    }
}
