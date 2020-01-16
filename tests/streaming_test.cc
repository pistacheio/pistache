#include "gtest/gtest.h"

#include <pistache/client.h>
#include <pistache/description.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace std;
using namespace Pistache;

static constexpr size_t N_LETTERS = 26;
static constexpr size_t LETTER_REPEATS = 100000;
static constexpr size_t SET_REPEATS = 10;
static constexpr size_t N_WORKERS = 10;

void dumpData(const Rest::Request & /*req*/, Http::ResponseWriter response) {
  using Lock = std::mutex;
  using Guard = std::lock_guard<Lock>;

  Lock responseLock;
  std::vector<std::thread> workers;
  std::condition_variable cv;

  std::queue<std::function<void()>> jobs;
  Lock jobLock;
  std::atomic<size_t> jobCounter(0);

  constexpr size_t JOB_LIMIT = SET_REPEATS * N_LETTERS;

  for (size_t j = 0; j < N_WORKERS; ++j) {
    workers.push_back(std::thread([&jobCounter, &cv, &jobLock, &jobs]() {
      while (jobCounter < JOB_LIMIT) {
        std::unique_lock<Lock> l(jobLock);
        cv.wait(l, [&jobCounter, &jobs] {
          return jobs.size() || !(jobCounter < JOB_LIMIT);
        });
        if (!jobs.empty()) {
          auto f = std::move(jobs.front());
          jobs.pop();
          l.unlock();
          f();
          ++jobCounter;
          l.lock();
        }
      }
      cv.notify_all();
    }));
  }

  auto stream = response.stream(Http::Code::Ok);
  const char letter = 'A';

  for (size_t s = 0; s < SET_REPEATS; ++s) {
    for (size_t i = 0; i < N_LETTERS; ++i) {
      auto job = [&stream, &responseLock, i]() -> void {
        constexpr size_t nchunks = 10;
        constexpr size_t chunk_size = LETTER_REPEATS / nchunks;
        const std::string payload(chunk_size, letter + i);
        {
          Guard guard(responseLock);
          for (size_t chunk = 0; chunk < nchunks; ++chunk) {
            stream.write(payload.c_str(), chunk_size);
            stream.flush();
          }
        }
      };
      std::unique_lock<Lock> l(jobLock);
      jobs.push(std::move(job));
      l.unlock();
      cv.notify_all();
    }
  }

  for (auto &w : workers) {
    w.join();
  }
  stream.ends();
}

TEST(streaming, from_description) {
  Address addr(Ipv4::any(), Port(0));
  const size_t threads = 20;

  std::shared_ptr<Http::Endpoint> endpoint;
  Rest::Description desc("Rest Description Test", "v1");
  Rest::Router router;

  desc.route(desc.get("/"))
      .bind(&dumpData)
      .response(Http::Code::Ok, "Response to the /ready call");

  router.initFromDescription(desc);

  auto flags = Tcp::Options::ReuseAddr;
  auto opts =
      Http::Endpoint::options().threads(threads).flags(flags).maxRequestSize(
          1024 * 1024);

  endpoint = std::make_shared<Pistache::Http::Endpoint>(addr);
  endpoint->init(opts);
  endpoint->setHandler(router.handler());
  endpoint->serveThreaded();

  std::stringstream ss;
  // from
  // https://stackoverflow.com/questions/6624667/can-i-use-libcurls-curlopt-writefunction-with-a-c11-lambda-expression#14720398
  typedef size_t (*CURL_WRITEFUNCTION_PTR)(void *, size_t, size_t, void *);

  auto curl_callback = [](void *ptr, size_t size, size_t nmemb,
                          void *stream) -> size_t {
    auto ss = static_cast<std::stringstream *>(stream);
    ss->write(static_cast<char *>(ptr), size * nmemb);
    return size * nmemb;
  };

  const auto port = endpoint->getPort();
  std::string url = "http://localhost:" + std::to_string(port) + "/";
  CURLcode res = CURLE_FAILED_INIT;
  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     static_cast<CURL_WRITEFUNCTION_PTR>(curl_callback));
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ss);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
  endpoint->shutdown();

  if (res != CURLE_OK)
    std::cerr << curl_easy_strerror(res) << std::endl;

  ASSERT_EQ(res, CURLE_OK);
  ASSERT_EQ(ss.str().size(), SET_REPEATS * LETTER_REPEATS * N_LETTERS);
}
