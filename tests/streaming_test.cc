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
        const std::string payload(chunk_size, static_cast<char>(letter + i));
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

// from
// https://stackoverflow.com/questions/6624667/can-i-use-libcurls-curlopt-writefunction-with-a-c11-lambda-expression#14720398
typedef size_t (*CURL_WRITEFUNCTION_PTR)(void *, size_t, size_t, void *);
auto curl_callback = [](void *ptr, size_t size, size_t nmemb,
                        void *stream) -> size_t {
  auto ss = static_cast<std::stringstream *>(stream);
  ss->write(static_cast<char *>(ptr), size * nmemb);
  return size * nmemb;
};

class StreamingTests : public testing::Test {
public:
  StreamingTests() : address(Pistache::Ipv4::any(), Pistache::Port(0)), endpoint(address), curl(curl_easy_init()) {
  }

  void SetUp() override {
    ASSERT_NE(nullptr, curl);
  }

  void TearDown() override {
    curl_easy_cleanup(curl);
    endpoint.shutdown();
  }

  void Init(const std::shared_ptr<Http::Handler>& handler) {
    auto flags = Tcp::Options::ReuseAddr;
    auto options = Http::Endpoint::options().threads(threads).flags(flags).maxRequestSize(1024 * 1024);

    endpoint.init(options);
    endpoint.setHandler(handler);
    endpoint.serveThreaded();

    url = "http://localhost:" + std::to_string(endpoint.getPort()) + "/";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     static_cast<CURL_WRITEFUNCTION_PTR>(curl_callback));
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ss);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  }

  Address address;
  Http::Endpoint endpoint;

  CURL *curl;
  std::string url;
  std::stringstream ss;

  static constexpr std::size_t threads = 20;
};

TEST_F(StreamingTests, FromDescription) {
  Rest::Description desc("Rest Description Test", "v1");
  Rest::Router router;

  desc.route(desc.get("/"))
      .bind(&dumpData)
      .response(Http::Code::Ok, "Response to the /ready call");

  router.initFromDescription(desc);
  Init(router.handler());

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK)
    std::cerr << curl_easy_strerror(res) << std::endl;

  ASSERT_EQ(res, CURLE_OK);
  ASSERT_EQ(ss.str().size(), SET_REPEATS * LETTER_REPEATS * N_LETTERS);
}

class HelloHandler : public Http::Handler {
public:
  HTTP_PROTOTYPE(HelloHandler)

  void onRequest(const Http::Request&, Http::ResponseWriter response) override
  {
    auto stream = response.stream(Http::Code::Ok);
    stream << "Hello ";
    stream.flush();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    stream << "world!";
    stream.ends();
  }
};

TEST_F(StreamingTests, ChunkedStream) {
  // force unbuffered
  curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1);

  Init(std::make_shared<HelloHandler>());

  std::thread thread([&]() {
    curl_easy_perform(curl);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ("Hello ", ss.str());
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  EXPECT_EQ("Hello world!", ss.str());

  thread.join();
}
