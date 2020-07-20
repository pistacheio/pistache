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
#include <string>
#include <condition_variable>

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

namespace {
  struct SyncContext {
    std::mutex m;
    std::condition_variable cv;
    bool flag = false;
  };

  using Chunks = std::vector<std::string>;

  std::string chunksToString(const Chunks& chunks) {
    std::string result;

    for (const auto &chunk : chunks) {
      result += chunk;
    }

    return result;
  }
}

// from
// https://stackoverflow.com/questions/6624667/can-i-use-libcurls-curlopt-writefunction-with-a-c11-lambda-expression#14720398
typedef size_t (*CURL_WRITEFUNCTION_PTR)(void *, size_t, size_t, void *);
auto curl_callback = [](void *ptr, size_t size, size_t nmemb,
                        void *userdata) -> size_t {
  auto chunks = static_cast<Chunks*>(userdata);
  chunks->emplace_back(static_cast<char *>(ptr), size * nmemb);
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
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunks);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  }

  Address address;
  Http::Endpoint endpoint;

  CURL *curl;
  std::string url;
  Chunks chunks;

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
  ASSERT_EQ(chunksToString(chunks).size(), SET_REPEATS * LETTER_REPEATS * N_LETTERS);
}

class HelloHandler : public Http::Handler {
public:
  HTTP_PROTOTYPE(HelloHandler)

  explicit HelloHandler(SyncContext& ctx)
    : ctx_{ctx}
    {}

  void onRequest(const Http::Request&, Http::ResponseWriter response) override
  {
    std::unique_lock<std::mutex> lk(ctx_.m);
    auto stream = response.stream(Http::Code::Ok);

    stream << "Hello ";
    stream.flush();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    stream << "world";
    stream.flush();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    stream << "!";
    stream.ends();

    ctx_.flag = true;
    lk.unlock();
    ctx_.cv.notify_one();
  }

private:
  SyncContext& ctx_;
};

TEST_F(StreamingTests, ChunkedStream) {
  SyncContext ctx;

  // force unbuffered
  curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1);

  Init(std::make_shared<HelloHandler>(ctx));

  std::thread thread([&]() {
    curl_easy_perform(curl);
  });

  std::unique_lock<std::mutex> lk{ctx.m};
  ctx.cv.wait(lk, [&ctx]{ return ctx.flag; });

  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  if (thread.joinable()) {
    thread.join();
  }

  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(chunks[0], "Hello ");
  EXPECT_EQ(chunks[1], "world");
  EXPECT_EQ(chunks[2], "!");
}
