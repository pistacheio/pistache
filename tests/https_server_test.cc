#include <pistache/http.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

#include "gtest/gtest.h"

#include <curl/curl.h>

using namespace Pistache;

#define ADDRESS "localhost:907"

/* _ALL_ those tests should fail around 2020, when the certificates will expire */

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

struct HelloHandler : public Http::Handler {
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request&, Http::ResponseWriter writer) {
        writer.send(Http::Code::Ok, "Hello, World!");
    }
};

TEST(http_client_test, basic_tls_request) {
    Http::Endpoint server(ADDRESS "1");
    auto           flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto           server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.serveThreaded();

    CURL        *curl;
    CURLcode    res;
    std::string buffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://" ADDRESS "1");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    server.shutdown();
}

TEST(http_client_test, basic_tls_request_with_auth) {
    Http::Endpoint server(ADDRESS "2");
    auto           flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto           server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL        *curl;
    CURLcode    res;
    std::string buffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://" ADDRESS "2");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    server.shutdown();
}

TEST(http_client_test, basic_tls_request_with_auth_no_client_cert) {
    Http::Endpoint server(ADDRESS "3");
    auto           flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto           server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL        *curl;
    CURLcode    res;
    std::string buffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://" ADDRESS "3");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    ASSERT_NE(res, CURLE_OK);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    server.shutdown();
}

TEST(http_client_test, basic_tls_request_with_auth_client_cert_not_signed) {
    Http::Endpoint server(ADDRESS "4");
    auto           flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto           server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt");
    server.serveThreaded();

    CURL        *curl;
    CURLcode    res;
    std::string buffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://" ADDRESS "4");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client_not_signed.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client_not_signed.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    ASSERT_NE(res, CURLE_OK);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    server.shutdown();
}

static bool callback_called = false;
static int verify_callback(int verify, void *ctx)
{
    (void)verify;
    (void)ctx;

    callback_called = true;
    return 1;
}


TEST(http_client_test, basic_tls_request_with_auth_with_cb) {
    Http::Endpoint server(ADDRESS "5");
    auto           flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;
    auto           server_opts = Http::Endpoint::options().flags(flags);

    server.init(server_opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.useSSL("./certs/server.crt", "./certs/server.key");
    server.useSSLAuth("./certs/rootCA.crt", "./certs", &verify_callback);
    server.serveThreaded();

    CURL        *curl;
    CURLcode    res;
    std::string buffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    ASSERT_NE(curl, nullptr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://" ADDRESS "5");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, "./certs/client.crt");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, "./certs/client.key");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "./certs/rootCA.crt");

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    /* Skip hostname check */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);
    ASSERT_EQ(res, CURLE_OK);
    ASSERT_EQ(buffer, "Hello, World!");
    ASSERT_EQ(callback_called, true);

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    callback_called = false;

    server.shutdown();
}

