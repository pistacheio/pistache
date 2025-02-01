/*
 * SPDX-FileCopyrightText: 2018 knowledge4igor
 *
 * SPDX-License-Identifier: Apache-2.0
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

/* Sept/2024: In Windows, if basic_tls_request_with_auth and
 * basic_tls_request_with_auth_with_cb fail, you may need to uninstall the
 * default (schannel) libcurl, and install the openssl one instead:
 *   vcpkg remove curl
 *   vcpkg install curl[openssl]
 *
 * See https://github.com/openssl/openssl/issues/25520 for more details
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

    auto rb       = client.get(server_address);
    auto response = rb.header<Http::Header::Connection>(
        Http::ConnectionControl::KeepAlive).send();
    bool done = false;
    response.then(
        [&done](Http::Response rsp) {
            PS_LOG_DEBUG_ARGS("http rsp code: %d", rsp.code());
            if (rsp.code() == Http::Code::Ok)
            {
                done = true;
                // const std::string bdy(rsp.body());
            }
        },
        Async::IgnoreException);

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    client.shutdown();

    ASSERT_TRUE(done);
}

TEST(https_client_test, one_client_with_bad_google_request)
{
    PS_TIMEDBG_START;

    const std::string server_address(
        "https://www.google.com/bunkgwl?bunkeno=pistache+HTTP+REST");

    Http::Experimental::Client client;
    client.init();

    auto rb       = client.get(server_address);
    auto response = rb.header<Http::Header::Connection>(
        Http::ConnectionControl::KeepAlive) .send();
    bool done      = false;
    bool error_404 = false;

    response.then(
        [&done, &error_404](Http::Response rsp) {
            PS_LOG_DEBUG_ARGS("http rsp code (expect 404): %d", rsp.code());
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

            auto response = rb.header<Http::Header::Connection>(
                                  Http::ConnectionControl::KeepAlive)
                                .send();
            response.then(
                [&response_counter, &response_correct_counter,
                 server_address_idx,
                 i,
#ifdef DEBUG
                 j,
#endif
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
                                PS_LOG_DEBUG_ARGS(
                                    "For i=%d, j=%d, %s not found in resp %s",
                                    i, j,
                                    resp_substr[i].c_str(), bdy.c_str());
                        }

                    }
                    else
                        PS_LOG_WARNING_ARGS("Http::Response error code %d",
                                            rsp.code());
                },
                Async::IgnoreException);
            responses.push_back(std::move(response));
        }
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(15));

    for (unsigned int j = 0; j < CLIENT_SIZE; ++j)
    {
        client[j].shutdown();
    }

    ASSERT_EQ(response_counter, CLIENT_SIZE * RESPONSE_SIZE);

    PS_LOG_DEBUG_ARGS("For aol.com, response_correct_counter %d, max %d",
                      static_cast<int>(response_correct_counter),
                      ((CLIENT_SIZE * RESPONSE_SIZE) / SERVER_ADDRESS_NUM));
    // Note: We allow response_correct_counter to be somewhat less than
    // "CLIENT_SIZE * RESPONSE_SIZE / SERVER_ADDRESS_NUM", because both Bing
    // and Google appeared to intermittently return flaky results sometimes -
    // pages that offered to redirect us to their AI result and so on and which
    // didn't actually include the query answer as far as I could see. Now that
    // we're checking aol.com query results instead of Google/Bing, we keep the
    // same logic (of allowing some query results not to have an answer)
    // just-in-case.
    unsigned int response_correct_counter_uint =
        static_cast<unsigned int>(response_correct_counter);
    ASSERT_GE(response_correct_counter_uint,
              (((CLIENT_SIZE * RESPONSE_SIZE) * 2) / 3) / SERVER_ADDRESS_NUM);
}

TEST(https_client_test, one_client_with_one_request)
{
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
        Pistache::Http::Experimental::SslVerification::Off);
    client.init(opts);

    auto rb       = client.get(server_address);
    auto response = rb.header<Http::Header::Connection>(Http::ConnectionControl::KeepAlive)
                        .send();
    bool done = false;
    response.then(
        [&done](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                done = true;
        },
        Async::IgnoreException);

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(done);
}

TEST(https_client_test, ssl_verify_locations)
{
    PS_TIMEDBG_START;

    std::string host_chain_pem_file(
        Http::Experimental::Connection::getHostChainPemFile());

    ASSERT_FALSE(host_chain_pem_file.empty());

    SSL_CTX* ctx = NULL;

    do
    {
        /* https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
        const SSL_METHOD* method = SSLv23_method();
        unsigned long ssl_err    = ERR_get_error();

        if (NULL == method)
        {
            PS_LOG_DEBUG_ARGS("SSLv23_method ssl_err: %d", ssl_err);
            ASSERT_FALSE(NULL == method);
            break;
        }

        /* http://www.openssl.org/docs/ssl/ctx_new.html */
        ctx = SSL_CTX_new(method);
        /* ctx = SSL_CTX_new(TLSv1_method()); */
        ssl_err = ERR_get_error();

        if (NULL == ctx)
        {
            PS_LOG_DEBUG_ARGS("SSL_CTX_new ssl_err: %d", ssl_err);
            ASSERT_FALSE(NULL == ctx);
            break;
        }

        // http://www.openssl.org/docs/ssl/SSL_CTX_load_verify_locations.html
        int res = SSL_CTX_load_verify_locations(ctx,
                                                host_chain_pem_file.c_str(),
                                                NULL);
        ssl_err = ERR_get_error();

        if (!(1 == res))
            PS_LOG_INFO_ARGS("SSL_CTX_load_verify_locations ssl_err: %d",
                             ssl_err);

        ASSERT_TRUE(res == 1);

        break;
    } while (true);

    if (ctx)
        SSL_CTX_free(ctx);
}

TEST(https_client_test, one_client_with_multiple_requests)
{
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
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 3;
    int response_counter    = 0;

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.send();
        response.then(
            [&response_counter](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++response_counter;
            },
            Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(response_counter, RESPONSE_SIZE);
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

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.send();
        response.then(
            [&response_counter](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++response_counter;
            },
            Async::IgnoreException);

        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(response_counter, 0);
}

TEST(https_client_test, multiple_clients_with_one_request)
{
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

    const int CLIENT_SIZE = 3;
    Http::Experimental::Client client1;
    client1.init();
    Http::Experimental::Client client2;
    client2.init();
    Http::Experimental::Client client3;
    client3.init();

    std::vector<Async::Promise<Http::Response>> responses;
    std::atomic<int> response_counter(0);

    auto rb1       = client1.get(server_address);
    auto response1 = rb1.send();
    response1.then(
        [&response_counter](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        },
        Async::IgnoreException);
    responses.push_back(std::move(response1));
    auto rb2       = client2.get(server_address);
    auto response2 = rb2.send();
    response2.then(
        [&response_counter](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        },
        Async::IgnoreException);
    responses.push_back(std::move(response2));
    auto rb3       = client3.get(server_address);
    auto response3 = rb3.send();
    response3.then(
        [&response_counter](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                ++response_counter;
        },
        Async::IgnoreException);
    responses.push_back(std::move(response3));

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client1.shutdown();
    client2.shutdown();
    client3.shutdown();

    ASSERT_EQ(response_counter, CLIENT_SIZE);
}

TEST(https_client_test, timeout_reject)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<DelayHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    client.init();

    auto rb       = client.get(server_address).timeout(std::chrono::milliseconds(1000));
    auto response = rb.header<Http::Header::Connection>(Http::ConnectionControl::KeepAlive)
                        .send();
    bool is_reject = false;
    response.then([&is_reject](Http::Response /*rsp*/) { is_reject = false; },
                  [&is_reject](std::exception_ptr /*exc*/) { is_reject = true; });

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(is_reject);
}

TEST(
    https_client_test,
    one_client_with_multiple_requests_and_one_connection_per_host_and_two_threads)
{
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
    auto opts = Http::Experimental::Client::options().maxConnectionsPerHost(1).threads(2);
    client.init(opts);

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 6;
    std::atomic<int> response_counter(0);

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.header<Http::Header::Connection>(Http::ConnectionControl::KeepAlive)
                            .send();
        response.then(
            [&](Http::Response rsp) {
                PS_LOG_DEBUG("Http::Response");

                if (rsp.code() == Http::Code::Ok)
                    ++response_counter;
                else
                    PS_LOG_DEBUG_ARGS("Http::Response error code %d",
                                      rsp.code());
            },
            Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(response_counter, RESPONSE_SIZE);
}

TEST(
    https_client_test,
    one_client_with_multiple_requests_and_two_connections_per_host_and_one_thread)
{
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
    auto opts = Http::Experimental::Client::options().maxConnectionsPerHost(2).threads(1);
    client.init(opts);

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE = 6;
    std::atomic<int> response_counter(0);

    auto rb = client.get(server_address);
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        auto response = rb.header<Http::Header::Connection>(Http::ConnectionControl::KeepAlive)
                            .send();
        response.then(
            [&](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                    ++response_counter;
#ifdef DEBUG
                else
                    PS_LOG_WARNING_ARGS("Http failure code %d", rsp.code());
#endif
            },
            Async::IgnoreException);
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_EQ(response_counter, RESPONSE_SIZE);
}

TEST(https_client_test, test_client_timeout)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags).threads(4);
    server.init(server_opts);
    server.setHandler(Http::make_handler<FastEvenPagesHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    client.init();

    std::vector<Async::Promise<Http::Response>> responses;
    const int RESPONSE_SIZE         = 4;
    int rejects_counter             = 0;
    const std::vector<int> timeouts = { 0, 1000, 4500, 1000 };

    std::map<int, std::string> res;
    for (int i = 0; i < RESPONSE_SIZE; ++i)
    {
        const std::string page = server_address + "/" + std::to_string(i);
        auto rb                = client.get(page).timeout(std::chrono::milliseconds(timeouts[i]));
        auto response          = rb.send();
        response.then(
            [&res, num = i](Http::Response rsp) {
                if (rsp.code() == Http::Code::Ok)
                {
                    PS_LOG_DEBUG_ARGS("Http::Response num %d", num);
                    res[num] = rsp.body();
                }
#ifdef DEBUG
                else
                {
                    PS_LOG_DEBUG_ARGS("Http::Response num %d resp code %d",
                                      num, rsp.code());
                }
#endif
            },
            [&rejects_counter
#ifdef DEBUG
             ,
             num = i
#endif
        ](std::exception_ptr) {
                PS_LOG_DEBUG_ARGS("Http::Response reject num %d", num);
                ++rejects_counter;
            });
        responses.push_back(std::move(response));
    }

    auto sync = Async::whenAll(responses.begin(), responses.end());
    Async::Barrier<std::vector<Http::Response>> barrier(sync);

    barrier.wait_for(std::chrono::seconds(2));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    server.shutdown();
    client.shutdown();

    ASSERT_GE(rejects_counter, 1);
    ASSERT_EQ(res.size(), 2u);

    auto it1 = res.find(0);
    ASSERT_NE(it1, res.end());
    ASSERT_EQ(it1->second, "0");

    auto it2 = res.find(2);
    ASSERT_NE(it2, res.end());
    ASSERT_EQ(it2->second, "2");
}

TEST(https_client_test, client_sends_query)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<QueryBounceHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    client.init();

    std::string queryStr;
    Http::Uri::Query query(
        { { "param1", "1" }, { "param2", "3.14" }, { "param3", "a+string" } });

    auto rb       = client.get(server_address);
    auto response = rb.params(query).send();

    response.then(
        [&queryStr](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
                queryStr = rsp.body();
        },
        Async::IgnoreException);

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    EXPECT_EQ(queryStr[0], '?');

    std::unordered_map<std::string, std::string> results;
    bool key = true;
    std::string keyStr, valueStr;

    for (auto it = std::next(queryStr.begin()); it != queryStr.end(); it++)
    {
        if (*it == '&' || std::next(it) == queryStr.end())
        {
            if (*it != '&')
                valueStr += *it;
            results[keyStr] = valueStr;
            keyStr          = "";
            valueStr        = "";
            key             = true;
        }
        else if (*it == '=')
            key = false;
        else if (key)
            keyStr += *it;
        else
            valueStr += *it;
    }

    EXPECT_EQ(static_cast<long int>(results.size()),
              std::distance(query.parameters_begin(), query.parameters_end()));

    for (auto entry : results)
    {
        ASSERT_TRUE(query.has(entry.first));
        EXPECT_EQ(entry.second, query.get(entry.first).value());
    }
}

TEST(https_client_test, client_get_large_content)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<LargeContentHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    auto opts = Http::Experimental::Client::options().maxResponseSize(8192);
    client.init(opts);

    auto response = client.get(server_address).send();
    bool done     = false;
    std::string rcvContent;
    response.then(
        [&done, &rcvContent](Http::Response rsp) {
            if (rsp.code() == Http::Code::Ok)
            {
                done       = true;
                rcvContent = rsp.body();
            }
        },
        Async::IgnoreException);

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_TRUE(done);
    ASSERT_EQ(largeContent, rcvContent);
}

TEST(https_client_test, client_do_not_get_large_content)
{
    PS_TIMEDBG_START;

    const Pistache::Address address("localhost", Pistache::Port(0));

    Http::Endpoint server(address);
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);
    server.init(server_opts);
    server.setHandler(Http::make_handler<LargeContentHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    const std::string server_address = getServerUrl(server);

    Http::Experimental::Client client;
    auto opts = Http::Experimental::Client::options().maxResponseSize(4096);
    client.init(opts);

    auto response       = client.get(server_address).send();
    bool ok_flag        = false;
    bool exception_flag = false;
    response.then(
        [&ok_flag](Http::Response /*rsp*/) { ok_flag = true; },
        [&exception_flag](std::exception_ptr /*ptr*/) { exception_flag = true; });

    Async::Barrier<Http::Response> barrier(response);
    barrier.wait_for(std::chrono::seconds(5));

    server.shutdown();
    client.shutdown();

    ASSERT_FALSE(ok_flag);
    ASSERT_TRUE(exception_flag);
}
