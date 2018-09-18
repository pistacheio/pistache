#include "gtest/gtest.h"

#include <pistache/http.h>
#include <pistache/description.h>
#include <pistache/client.h>
#include <pistache/endpoint.h>

#include <curl/curl.h>
#include <curl/easy.h>

using namespace std;
using namespace Pistache;

void dumpData(const Rest::Request&req, Http::ResponseWriter response) {
    UNUSED(req);

    auto stream = response.stream(Http::Code::Ok);

    std::streamsize n = 1000;
    char data = 'A';
    for (size_t i ; i < 26; ++i) {
        std::cout << "Sending " << n << " bytes of " << data << std::endl;
        std::string payload(data++, n);
        stream.write(payload.c_str(), n);
        stream.flush();
    }
    stream.ends();
}

TEST(stream, from_description)
{
    Http::Client client;
    auto client_opts = Http::Client::options()
        .threads(3)
        .maxConnectionsPerHost(3);
    client.init(client_opts);

    Address addr(Ipv4::any(), 9080);
    const size_t threads = 20;

    auto pid = fork();
    if ( pid == 0) {
        std::shared_ptr<Http::Endpoint> endpoint;
        Rest::Description desc("Rest Description Test", "v1");
        Rest::Router router;

        desc
            .route(desc.get("/"))
            .bind(&dumpData)
            .response(Http::Code::Ok, "Response to the /ready call");

        router.initFromDescription(desc);

        auto flags = Tcp::Options::InstallSignalHandler | Tcp::Options::ReuseAddr;

        auto opts = Http::Endpoint::options()
            .threads(threads)
            .flags(flags)
            .maxPayload(1024*1024)
            ;

        endpoint = std::make_shared<Pistache::Http::Endpoint>(addr);
        endpoint->init(opts);
        endpoint->setHandler(router.handler());
        endpoint->serve();
        endpoint->shutdown();
        return;
    }

    std::stringstream ss;
    // from https://stackoverflow.com/questions/6624667/can-i-use-libcurls-curlopt-writefunction-with-a-c11-lambda-expression#14720398
    typedef size_t(*CURL_WRITEFUNCTION_PTR)(void*, size_t, size_t, void*);

    auto curl_callback = [](void *ptr, size_t size, size_t nmemb, void *stream) -> size_t {
        std::cout.write((char *)ptr, size * nmemb);
        auto ss = static_cast<std::stringstream *>(stream);
        (*ss).write(static_cast<char *>(ptr), size* nmemb);
        return size * nmemb;
    };

    CURLcode res;
    CURL * curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9080/");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, static_cast<CURL_WRITEFUNCTION_PTR>(curl_callback));
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ss);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cout << "Curl failed: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }

    std::cout << "GOT HERE" << std::endl;

    std::cout << ss.str() << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    kill(pid, SIGTERM);
    int r;
    waitpid(pid, &r, 0);
    client.shutdown();
}
