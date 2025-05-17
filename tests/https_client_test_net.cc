/*
 * SPDX-FileCopyrightText: 2018 knowledge4igor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file contains tests that require Internet access, e.g. tests that fetch
 * from google.com. Note that on launchpad-debian builder we exclude these
 * tests.
 */

#include <pistache/client.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

// These openssl header files are not required for the Pistache HTTPS
// client. However, we use them in the "ssl_verify_locations" in this file to
// check that the cacert file has been correctly loaded.
#include <openssl/err.h>
#include <openssl/ssl.h>

using namespace Pistache;

/* Should these tests fail, please re-run "./new-certs.sh" from the "./certs"
 * directory.
 */

static std::string getServerUrl(const Http::Endpoint& server)
{
    return std::string("https://localhost:") + server.getPort().toString();
}

struct HelloHandler : public Http::Handler
{
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& /*request*/,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

struct DelayHandler : public Http::Handler
{
    HTTP_PROTOTYPE(DelayHandler)

    void onRequest(const Http::Request& /*request*/,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        std::this_thread::sleep_for(std::chrono::seconds(4));
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

struct FastEvenPagesHandler : public Http::Handler
{
    HTTP_PROTOTYPE(FastEvenPagesHandler)

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        std::string page = request.resource();
        PS_LOG_DEBUG_ARGS("page: %s", page.c_str());
        page.erase(0, 1);
        int num = std::stoi(page);
        PS_LOG_DEBUG_ARGS("page after erase(0, 1): %s; num: %d",
                          page.c_str(), num);

        if (num % 2 != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            writer.send(Http::Code::Ok, std::to_string(num));
        }
        else
        {
            writer.send(Http::Code::Ok, std::to_string(num));
        }
    }
};

struct QueryBounceHandler : public Http::Handler
{
    HTTP_PROTOTYPE(QueryBounceHandler)

    void onRequest(const Http::Request& request,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        writer.send(Http::Code::Ok, request.query().as_str());
    }
};

namespace
{
    std::string largeContent(4097, 'a');
}

struct LargeContentHandler : public Http::Handler
{
    HTTP_PROTOTYPE(LargeContentHandler)

    void onRequest(const Http::Request& /*request*/,
                   Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        writer.send(Http::Code::Ok, largeContent);
    }
};

// MUST be FIRST test
TEST(https_client_test, first_client_global_init)
{
    Http::Experimental::Connection::setHostChainPemFile("./certs/cacert.pem");
}

TEST(https_client_test, one_client_with_google_request)
{
    PS_TIMEDBG_START;

    const std::string server_address(
        "https://www.google.com/search?q=pistache+HTTP+REST");

    Http::Experimental::Client client;
    client.init();

    bool done = false;
    auto rb       = client.get(server_address);
    try {
        auto response = rb.header<Http::Header::Connection>(
            Http::ConnectionControl::KeepAlive).send();

        response.then(
            [&done](Http::Response rsp) {
                PS_LOG_DEBUG_ARGS("http rsp code: %d", rsp.code());
                if (rsp.code() == Http::Code::Ok)
                {
                    done = true;
                    // const std::string bdy(rsp.body());
                }
                else if (rsp.code() == Http::Code::Found)
                {
                    // Feb-2025: These HTTP 302 (temporarily moved aka Found)
                    // responses seem to come very roughly once every 3000
                    // Google search requests
                    PS_LOG_INFO(
                        "Temporarily Moved (aka Found) from google.com");
                    done = true;
                }
                else if ((rsp.code() == Http::Code::Temporary_Redirect) ||
                         (rsp.code() == Http::Code::See_Other))
                {
                    // Feb-2025: We have not seen these HTTP 307 (Temporary
                    // Redirect) or HTTP 303 (See Other) responses from
                    // google.com, but include them here since they are so
                    // similar to HTTP 302, which we do see.
                    PS_LOG_INFO(
                        "Temporary Redirect or See Other from google.com");
                    done = true;
                }
            },
            Async::IgnoreException);

        Async::Barrier<Http::Response> barrier(response);
        barrier.wait_for(std::chrono::seconds(5));
    }
    catch (const std::exception& e)
    {
        PS_LOG_WARNING_ARGS("Exception fetching from google.com: %s",
                            e.what());
        // This can happen if google.com is unreachable, e.g. we have no
        // network connection
    }

    client.shutdown();

    ASSERT_TRUE(done);
}

TEST(https_client_test, one_client_with_nonexisitent_url_request)
{
    PS_TIMEDBG_START;

    const std::string server_address(
        "https://www.gog27isnothere2xajsh.com/search?q=pistache+HTTP+REST");

    Http::Experimental::Client client;
    client.init();

    bool done  = false;
    bool excep = false;
    auto rb       = client.get(server_address);
    try {
        auto response = rb.header<Http::Header::Connection>(
            Http::ConnectionControl::KeepAlive).send();

        response.then(
            [&done](Http::Response rsp) {
                PS_LOG_DEBUG_ARGS("http rsp code: %d", rsp.code());
                if (rsp.code() == Http::Code::Ok)
                {
                    done = true;
                    // const std::string bdy(rsp.body());
                }
                else if (rsp.code() == Http::Code::Found)
                {
                    PS_LOG_INFO("Temporarily Moved (aka Found)");
                    done = true;
                }
                else if ((rsp.code() == Http::Code::Temporary_Redirect) ||
                         (rsp.code() == Http::Code::See_Other))
                {
                    PS_LOG_INFO("Temporary Redirect or See Other");
                    done = true;
                }
            },
            Async::IgnoreException);

        Async::Barrier<Http::Response> barrier(response);
        barrier.wait_for(std::chrono::seconds(5));
    }
    catch (const std::exception& e)
    {
        PS_LOG_DEBUG_ARGS("Exception fetching from nonexisitent URL: %s",
                          e.what());
        excep = true;
    }

    client.shutdown();

    ASSERT_TRUE(excep);
    ASSERT_FALSE(done);
}

TEST(https_client_test, one_client_with_bad_google_request)
{
    PS_TIMEDBG_START;

    const std::string server_address(
        "https://www.google.com/bunkgwl?bunkeno=pistache+HTTP+REST");

    Http::Experimental::Client client;
    client.init();

    auto rb        = client.get(server_address);
    bool done      = false;
    bool error_404 = false;

    try {
        auto response = rb.header<Http::Header::Connection>(
            Http::ConnectionControl::KeepAlive) .send();

        response.then(
            [&done, &error_404](Http::Response rsp) {
                PS_LOG_DEBUG_ARGS("http rsp code (expect 404): %d",
                                  rsp.code());
                if (rsp.code() == Http::Code::Ok)
                {
                    done = true;
                }
                else if (rsp.code() == Http::Code::Not_Found)
                {
                    error_404 = true;
                }
            },
            Async::IgnoreException);

        Async::Barrier<Http::Response> barrier(response);
        barrier.wait_for(std::chrono::seconds(5));
    }
    catch (const std::exception& e)
    {
        PS_LOG_WARNING_ARGS("Exception fetching from google.com: %s",
                            e.what());
        // This can happen if google.com is unreachable, e.g. we have no
        // network connection
    }

    client.shutdown();

    ASSERT_FALSE(done);
    ASSERT_TRUE(error_404);
}


TEST(https_client_test, multiple_clients_with_multiple_search_requests)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    const int RESPONSE_SIZE = 6;

    const std::string query[RESPONSE_SIZE] = {
        "when+were+the+first+moon+landings",
        "who+was+the+first+man+on+the+moon",
        "which+is+the+suns+largest+planet",
        "when+did+William+the+Conqueror+invade+England",
        "where+was+Shakespeare+born",
        "who+was+the+first+president+of+the+USA"
    };
    const std::string resp_substr[RESPONSE_SIZE] = {
        "1969",
        "armstrong",
        "jupiter",
        "1066",
        "stratford",
        "washington"
    };

    // Note: You can see raw web-page for a query by doing some like:
    //   curl "https://www.google.com/search?q=pistache+HTTP+REST"
    //
    // Unfortunately, as of Jan 2025, Google, Bing and many other search
    // engines do not have easily scannable plain-text in their web-page
    // responses; they send back blobs of encoded data. We include aol.com as a
    // search engine so we can scan its search response and check that we have
    // been sent reasonable results.

    // Issue https search queries to aol, google and bing:
    const std::string server_address_start[3] {
        "https://search.aol.com/aol/search?q=", // must be first
        "https://www.google.com/search?q=",
        "https://www.bing.com/search?q="
    };
    const unsigned int SERVER_ADDRESS_NUM = (sizeof(server_address_start) /
        sizeof(server_address_start[0]));

    const int CLIENT_SIZE = 3;
    Http::Experimental::Client client[CLIENT_SIZE];
    for (unsigned int j = 0; j < CLIENT_SIZE; ++j)
    {
        client[j].init();
    }

    std::vector<Async::Promise<Http::Response>> responses;
    std::atomic<int> response_counter(0);
    std::atomic<int> response_correct_counter(0);
    for (unsigned int j = 0; j < CLIENT_SIZE; ++j)
    {
        for (unsigned int i = 0; i < RESPONSE_SIZE; ++i)
        {
            const unsigned int server_address_idx =
                ((i + j) % SERVER_ADDRESS_NUM);

            const std::string server_address(
                server_address_start[server_address_idx] + query[i]);
            auto rb = client[j].get(server_address);

            try {
                auto response = rb.header<Http::Header::Connection>(
                    Http::ConnectionControl::KeepAlive)
                    .send();
                response.then(
                    [&response_counter, &response_correct_counter,
                     server_address_idx,
                     i,
                     j,
                     resp_substr](Http::Response rsp) {
                        PS_LOG_DEBUG("Http::Response");

                        if (rsp.code() == Http::Code::Ok)
                        {
                            ++response_counter;
                            if (0 == server_address_idx)
                            {
                                const std::string bdy(rsp.body());
                                auto it = std::search(
                                    bdy.begin(), bdy.end(),
                                    resp_substr[i].begin(), resp_substr[i].end(),
                                    [](unsigned char ch1, unsigned char ch2)
                                        { return std::toupper(ch1) == std::toupper(ch2);
                                        });
                                if (it != bdy.end())
                                    ++response_correct_counter;
                                else
                                    PS_LOG_WARNING_ARGS(
                                        "For i=%d, j=%d, %s "
                                        "not found in resp %s",
                                        i, j,
                                        resp_substr[i].c_str(), bdy.c_str());
                            }

                        }
                        else if ((rsp.code() == Http::Code::Found) ||
                                 (rsp.code() == Http::Code::Temporary_Redirect) ||
                                 (rsp.code() == Http::Code::See_Other))
                        {
                            // Feb-2025: See prior comment re:
                            // Http::Code::Found
                            PS_LOG_INFO("Temporary redirect");
                            ++response_counter;
                        }
                        else
                        {
                            PS_LOG_WARNING_ARGS("Http::Response error code %d",
                                                rsp.code());
                        }
                    },
                    Async::IgnoreException);
                responses.push_back(std::move(response));
            }
            catch (const std::exception& e)
            {
                PS_LOG_WARNING_ARGS("Exception fetching from %s: %s",
                                    server_address.c_str(), e.what());
                // This can happen if URL is unreachable, e.g. we have no
                // network connection
            }
        }
    }

    if (!responses.empty())
    {
        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);

        barrier.wait_for(std::chrono::seconds(15));
    }

    for (unsigned int j = 0; j < CLIENT_SIZE; ++j)
    {
        client[j].shutdown();
    }

    ASSERT_GE(response_counter, RESPONSE_SIZE);
    if (response_counter < (CLIENT_SIZE * RESPONSE_SIZE))
    {
        // Very occasionally we see an HTTP 500 error come back
        PS_LOG_WARNING_ARGS("response_counter %d less than expected %d; "
                            "possible internal server error at search engine",
                            static_cast<int>(response_counter),
                            (CLIENT_SIZE * RESPONSE_SIZE));
    }
    else
    {
        unsigned int response_correct_counter_uint =
            static_cast<unsigned int>(response_correct_counter);

        if (response_correct_counter_uint <
            ((CLIENT_SIZE * RESPONSE_SIZE) / SERVER_ADDRESS_NUM))
        {
            PS_LOG_WARNING_ARGS("For aol, response_correct_counter %d, max %d",
                                static_cast<int>(response_correct_counter),
                                ((CLIENT_SIZE * RESPONSE_SIZE) /
                                 SERVER_ADDRESS_NUM));
        }
#ifdef DEBUG
        else
        {
            PS_LOG_DEBUG_ARGS("For aol, response_correct_counter %d",
                              static_cast<int>(response_correct_counter));
        }
#endif

        // Note @Feb/2025: We allow response_correct_counter to be less than
        // "CLIENT_SIZE * RESPONSE_SIZE / SERVER_ADDRESS_NUM", because AOL
        // appears to intermittently (1 time per 2,000 requests approx) return
        // flaky results - pages that don't actually include the query answer
        // in plain-text form as far as I could see. When this happens, we have
        // seen AOL return up to 3 (out of a possible 6) such "flaky" pages.
        //
        // @May/2025: AOL is now intermittently returning ALL flaky/unparsable
        // pages, so we've changed what was formerly an ASSERT_GE here to a
        // debug warning if the responses don't appear correct.
        if (response_correct_counter_uint <
            ((CLIENT_SIZE * RESPONSE_SIZE) / 3) / SERVER_ADDRESS_NUM)
        {
            PS_LOG_WARNING_ARGS("For aol, response_correct_counter %d < %d",
                                static_cast<int>(response_correct_counter),
                                (((CLIENT_SIZE * RESPONSE_SIZE) / 3) /
                                 SERVER_ADDRESS_NUM));
        }
    }
}

TEST(https_client_test, one_cli_mult_reqs_force_https_verification_that_fails)
{
    // Since we are forcing HTTPS verification here (i.e. forcing it even for
    // localhost), check that all requests fail

    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    auto opts = Http::Experimental::Client::options().clientSslVerification(
        Pistache::Http::Experimental::SslVerification::On);
    client.init(opts);

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 3;
    int response_counter    = 0;
    bool excep = false;

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        try {
        auto response = rb.send();
        response.then(
            [&response_counter](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++response_counter;
            },
            Async::IgnoreException);

        responses.push_back(std::move(response));
        }
        catch (const std::exception& e)
        {
            PS_LOG_WARNING_ARGS("Exception fetching from %s: %s",
                                server_address.c_str(), e.what());
            // This can happen if URL is unreachable, e.g. we have no network
            // connection

            excep = true;
        }
    }

    if (!responses.empty())
    {
        auto sync = Async::whenAll(responses.begin(), responses.end());
        Async::Barrier<std::vector<Http::Response>> barrier(sync);

        barrier.wait_for(std::chrono::seconds(5));
    }

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(response_counter, 0);
    ASSERT_FALSE(excep);
}
