/*
 * SPDX-FileCopyrightText: 2019 Louis Solofrizzo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>
#include <cstring>

#include <pistache/client.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>

#include <gtest/gtest.h>

#include <curl/curl.h>

using namespace Pistache;

/* Should these tests fail, please re-run "./new-certs.sh" from the "./certs"
 * directory.
 */

static size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp)
{
    (static_cast<std::string*>(userp))->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static std::string getServerUrl(const Http::Endpoint& server)
{
    return std::string("https://localhost:") + server.getPort().toString();
}

struct HelloHandler : public Http::Handler
{
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request&, Http::ResponseWriter writer) override
    {
        PS_TIMEDBG_START_THIS;

        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

struct ServeFileHandler : public Http::Handler
{
    HTTP_PROTOTYPE(ServeFileHandler)

    void onRequest(const Http::Request&, Http::ResponseWriter writer) override
    {
        Http::serveFile(writer, "./certs/rootCA.crt")
            .then(
                [](ssize_t bytes) {
                    std::cout << "Sent " << bytes << " bytes" << std::endl;
                },
                Async::NoExcept);
    }
};

// @March/2024
//
// In macOS, calling curl_global_init, and then curl_global_cleanup, for every
// single test does not work. On the second test to be run, it generates the
// following error in the Pistache code:
/*   listener.cc:691 in handleNewConnection(): SSL connection error: 0070E86F01000000:error:0A00041B:SSL routines:ssl3_read_bytes:tlsv1 alert decrypt error:ssl/record/rec_layer_s3.c:861:SSL alert number 51
 */
// In the openssl documentation, 51 decrypt_error is described as:
//   Failed handshake cryptographic operation, including being unable to
//   correctly verify a signature, decrypt a key exchange, or validate a
//   finished message.
// BTW, updating Pistache certs (new-certs.sh) makes no difference
//
// The same code on Linux works no problem.
//
// The curl documentation is (IMHO) unclear as to whether it is OK to call
// curl_global_init+curl_global_cleanup repeatedly.
// https://everything.curl.dev/libcurl/globalinit states:
//
//   curl_global_init initializes global state so you should only call it once,
//   and once your program is completely done using libcurl you can call
//   curl_global_cleanup()...
//
// Motivated by that statement, we have changed the code so curl_global_init is
// called only once for the whole program, not once per test. That works on
// BOTH macOS and Linux.
//
// In macOS, we have the following version of curl:
/*
curl 8.4.0 (x86_64-apple-darwin23.0) libcurl/8.4.0 (SecureTransport) LibreSSL/3.3.6 zlib/1.2.12 nghttp2/1.58.0
Release-Date: 2023-10-11
Protocols: dict file ftp ftps gopher gophers http https imap imaps ldap ldaps mqtt pop3 pop3s rtsp smb smbs smtp smtps telnet tftp
Features: alt-svc AsynchDNS GSS-API HSTS HTTP2 HTTPS-proxy IPv6 Kerberos Largefile libz MultiSSL NTLM NTLM_WB SPNEGO SSL threadsafe UnixSockets
*/
// and the following version of openssl:
// OpenSSL 3.2.0 23 Nov 2023 (Library: OpenSSL 3.2.0 23 Nov 2023)
//
// In Linux (Unbuntu), we have the following version of curl:
/*
curl 7.81.0 (x86_64-pc-linux-gnu) libcurl/7.81.0 OpenSSL/3.0.2 zlib/1.2.11 brotli/1.0.9 zstd/1.4.8 libidn2/2.3.2 libpsl/0.21.0 (+libidn2/2.3.2) libssh/0.9.6/openssl/zlib nghttp2/1.43.0 librtmp/2.3 OpenLDAP/2.5.17
Release-Date: 2022-01-05
Protocols: dict file ftp ftps gopher gophers http https imap imaps ldap ldaps mqtt pop3 pop3s rtmp rtsp scp sftp smb smbs smtp smtps telnet tftp
Features: alt-svc AsynchDNS brotli GSS-API HSTS HTTP2 HTTPS-proxy IDN IPv6 Kerberos Largefile libz NTLM NTLM_WB PSL SPNEGO SSL TLS-SRP UnixSockets zstd
 */
// and the following version of openssl:
// OpenSSL 3.0.2 15 Mar 2022 (Library: OpenSSL 3.0.2 15 Mar 2022)

// MUST be FIRST test
TEST(https_server_test, first_curl_global_init)
{
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);

    ASSERT_EQ(res, CURLE_OK);
}

TEST(https_server_test, basic_tls_request)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    // Skip hostname check
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    PS_LOG_DEBUG("curl_easy_perform");
    res = curl_easy_perform(curl);
    PS_LOG_DEBUG("curl_easy_perform done");

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
}

TEST(https_server_test, basic_tls_request_with_chained_server_cert)
{
    PS_TIMEDBG_START_THIS;
    PS_LOG_DEBUG("basic_tls_request_with_chained_server_cert");

    Http::Endpoint server(Address("localhost", Pistache::Port(0)));

    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server_from_intermediate_with_chain.crt",
                  "./certs/server_from_intermediate.key");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
}

TEST(https_server_test, basic_tls_request_with_auth)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
}

TEST(https_server_test, basic_tls_request_with_auth_no_client_cert)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_NE(res, CURLE_OK);
}

TEST(https_server_test, basic_tls_request_with_auth_client_cert_not_signed)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client_not_signed.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client_not_signed.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_NE(res, CURLE_OK);
}

static bool callback_called = false;
static int verify_callback(int verify, void* ctx)
{
    (void)verify;
    (void)ctx;

    callback_called = true;
    return 1;
}

TEST(https_server_test, basic_tls_request_with_auth_with_cb)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt", "./certs", &verify_callback);
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
    ASSERT_EQ(callback_called, true);
    callback_called = false;
}

TEST(https_server_test, basic_tls_request_with_servefile)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));
    auto flags       = Tcp::Options::ReuseAddr;
    auto server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<ServeFileHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    std::array<char, CURL_ERROR_SIZE> errorstring;
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorstring.data());
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, true);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        std::cerr << errorstring.data() << std::endl;
    }

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer.rfind("-----BEGIN CERTIFICATE-----", 0), 0u);
}

TEST(https_server_test, basic_tls_request_with_password_cert)
{
    Http::Endpoint server(Address("localhost", Pistache::Port(0)));

    const auto passwordCallback = [](char* buf, int size, int /*rwflag*/, void* /*u*/) -> int {
        static constexpr const char* const password = "test";
        std::strncpy(buf, password, size);
        return static_cast<int>(std::strlen(password));
    };

    server.init(Http::Endpoint::options().flags(Tcp::Options::ReuseAddr));
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server_protected.crt", "./certs/server_protected.key", false, passwordCallback);
    server.serveThreaded();

    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    const auto url = getServerUrl(server);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    server.shutdown();

    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
}

// MUST be LAST test
TEST(https_server_test, last_curl_global_cleanup)
{
    curl_global_cleanup();
    // Note: curl_global_cleanup has no return code (void)
}
