#include <pistache/async.h>
#include <pistache/client.h>
#include <pistache/common.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>

#include "gtest/gtest.h"

#include <chrono>
#include <fstream>
#include <future>
#include <string>

using namespace Pistache;

struct HelloHandlerWithDelay : public Http::Handler {
  HTTP_PROTOTYPE(HelloHandlerWithDelay)

  explicit HelloHandlerWithDelay(int delay = 0) : delay_(delay) {}

  void onRequest(const Http::Request & /*request*/,
                 Http::ResponseWriter writer) override {
    std::this_thread::sleep_for(std::chrono::seconds(delay_));
    writer.send(Http::Code::Ok, "Hello, World!");
  }

  int delay_;
};

constexpr char SLOW_PAGE[] = "/slowpage";

struct HandlerWithSlowPage : public Http::Handler {
  HTTP_PROTOTYPE(HandlerWithSlowPage)

  explicit HandlerWithSlowPage(int delay = 0) : delay_(delay) {}

  void onRequest(const Http::Request &request,
                 Http::ResponseWriter writer) override {
    std::string message;
    if (request.resource() == SLOW_PAGE) {
      std::this_thread::sleep_for(std::chrono::seconds(delay_));
      message = "Slow page content!\n";
    } else {
      message = "Hello, World!\n";
    }

    writer.send(Http::Code::Ok, message);
    std::cout << "[server] Sent: " << message;
  }

  int delay_;
};

struct FileHandler : public Http::Handler {
  HTTP_PROTOTYPE(FileHandler)

  explicit FileHandler(const std::string &fileName) : fileName_(fileName) {}

  void onRequest(const Http::Request & /*request*/,
                 Http::ResponseWriter writer) override {
    Http::serveFile(writer, fileName_)
        .then(
            [this](ssize_t bytes) {
              std::cout << "Sent " << bytes << " bytes from " << fileName_
                        << " file" << std::endl;
            },
            Async::IgnoreException);
  }

private:
  std::string fileName_;
};

struct AddressEchoHandler : public Http::Handler {
  HTTP_PROTOTYPE(AddressEchoHandler)

  AddressEchoHandler() {}

  void onRequest(const Http::Request &request,
                 Http::ResponseWriter writer) override {
    std::string requestAddress = request.address().host();
    writer.send(Http::Code::Ok, requestAddress);
    std::cout << "[server] Sent: " << requestAddress << std::endl;
  }
};

int clientLogicFunc(int response_size, const std::string &server_page,
                    int timeout_seconds, int wait_seconds) {
  Http::Client client;
  client.init();

  std::vector<Async::Promise<Http::Response>> responses;
  auto rb =
      client.get(server_page).timeout(std::chrono::seconds(timeout_seconds));
  int resolver_counter = 0;
  int reject_counter = 0;
  for (int i = 0; i < response_size; ++i) {
    auto response = rb.send();
    response.then(
        [&resolver_counter](Http::Response resp) {
          std::cout << "Response code is " << resp.code() << std::endl;
          if (resp.code() == Http::Code::Ok) {
            ++resolver_counter;
          }
        },
        [&reject_counter](std::exception_ptr exc) {
          PrintException excPrinter;
          std::cout << "Reject with reason: ";
          excPrinter(exc);
          ++reject_counter;
        });
    responses.push_back(std::move(response));
  }

  auto sync = Async::whenAll(responses.begin(), responses.end());
  Async::Barrier<std::vector<Http::Response>> barrier(sync);
  barrier.wait_for(std::chrono::seconds(wait_seconds));

  client.shutdown();

  std::cout << "resolves: " << resolver_counter
            << ", rejects: " << reject_counter
            << ", timeout: " << timeout_seconds << ", wait: " << wait_seconds
            << "\n";

  return resolver_counter;
}

TEST(http_server_test,
     client_disconnection_on_timeout_from_single_threaded_server) {
  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags);
  server.init(server_opts);
  const int ONE_SECOND_TIMEOUT = 1;
  const int SIX_SECONDS_DELAY = 6;
  server.setHandler(
      Http::make_handler<HelloHandlerWithDelay>(SIX_SECONDS_DELAY));
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  const int CLIENT_REQUEST_SIZE = 1;
  int counter = clientLogicFunc(CLIENT_REQUEST_SIZE, server_address,
                                ONE_SECOND_TIMEOUT, SIX_SECONDS_DELAY);

  server.shutdown();

  ASSERT_EQ(counter, 0);
}

TEST(
    http_server_test,
    client_multiple_requests_disconnection_on_timeout_from_single_threaded_server) {
  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags);
  server.init(server_opts);

  const int ONE_SECOND_TIMEOUT = 1;
  const int SIX_SECONDS_DELAY = 6;
  server.setHandler(
      Http::make_handler<HelloHandlerWithDelay>(SIX_SECONDS_DELAY));
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  const int CLIENT_REQUEST_SIZE = 3;
  int counter = clientLogicFunc(CLIENT_REQUEST_SIZE, server_address,
                                ONE_SECOND_TIMEOUT, SIX_SECONDS_DELAY);

  server.shutdown();

  ASSERT_EQ(counter, 0);
}

TEST(http_server_test, multiple_client_with_requests_to_multithreaded_server) {
  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags).threads(3);
  server.init(server_opts);
  server.setHandler(Http::make_handler<HelloHandlerWithDelay>());
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  const int NO_TIMEOUT = 0;
  const int SIX_SECONDS_TIMOUT = 6;
  const int FIRST_CLIENT_REQUEST_SIZE = 4;
  std::future<int> result1(std::async(clientLogicFunc,
                                      FIRST_CLIENT_REQUEST_SIZE, server_address,
                                      NO_TIMEOUT, SIX_SECONDS_TIMOUT));
  const int SECOND_CLIENT_REQUEST_SIZE = 5;
  std::future<int> result2(
      std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE, server_address,
                 NO_TIMEOUT, SIX_SECONDS_TIMOUT));

  int res1 = result1.get();
  int res2 = result2.get();

  server.shutdown();

  ASSERT_EQ(res1, FIRST_CLIENT_REQUEST_SIZE);
  ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);
}

TEST(http_server_test,
     multiple_client_with_different_requests_to_multithreaded_server) {
  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags).threads(4);
  server.init(server_opts);
  const int SIX_SECONDS_DELAY = 6;
  server.setHandler(Http::make_handler<HandlerWithSlowPage>(SIX_SECONDS_DELAY));
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  const int FIRST_CLIENT_REQUEST_SIZE = 1;
  const int FIRST_CLIENT_TIMEOUT = SIX_SECONDS_DELAY / 2;
  std::future<int> result1(std::async(
      clientLogicFunc, FIRST_CLIENT_REQUEST_SIZE, server_address + SLOW_PAGE,
      FIRST_CLIENT_TIMEOUT, SIX_SECONDS_DELAY));
  const int SECOND_CLIENT_REQUEST_SIZE = 2;
  const int SECOND_CLIENT_TIMEOUT = SIX_SECONDS_DELAY * 2;
  std::future<int> result2(
      std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE, server_address,
                 SECOND_CLIENT_TIMEOUT, 2 * SIX_SECONDS_DELAY));

  int res1 = result1.get();
  int res2 = result2.get();

  server.shutdown();

  if (hardware_concurrency() > 1) {
    ASSERT_EQ(res1, 0);
    ASSERT_EQ(res2, SECOND_CLIENT_REQUEST_SIZE);
  } else {
    ASSERT_TRUE(true);
  }
}

TEST(http_server_test, server_with_static_file) {
  const std::string data("Hello, World!");
  char fileName[PATH_MAX] = "/tmp/pistacheioXXXXXX";
  if (!mkstemp(fileName)) {
    std::cerr << "No suitable filename can be generated!" << std::endl;
  }
  std::cout << "Creating temporary file: " << fileName << std::endl;

  std::ofstream tmpFile;
  tmpFile.open(fileName);
  tmpFile << data;
  tmpFile.close();

  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags);
  server.init(server_opts);
  server.setHandler(Http::make_handler<FileHandler>(fileName));
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  Http::Client client;
  client.init();
  auto rb = client.get(server_address);
  auto response = rb.send();
  std::string resultData;
  response.then(
      [&resultData](Http::Response resp) {
        std::cout << "Response code is " << resp.code() << std::endl;
        if (resp.code() == Http::Code::Ok) {
          resultData = resp.body();
        }
      },
      Async::Throw);

  const int WAIT_TIME = 2;
  Async::Barrier<Http::Response> barrier(response);
  barrier.wait_for(std::chrono::seconds(WAIT_TIME));

  client.shutdown();
  server.shutdown();

  std::cout << "Deleting file " << fileName << std::endl;
  std::remove(fileName);

  ASSERT_EQ(data, resultData);
}

TEST(http_server_test, server_request_copies_address) {
  const Pistache::Address address("localhost", Pistache::Port(0));

  Http::Endpoint server(address);
  auto flags = Tcp::Options::ReuseAddr;
  auto server_opts = Http::Endpoint::options().flags(flags);
  server.init(server_opts);
  server.setHandler(Http::make_handler<AddressEchoHandler>());
  server.serveThreaded();

  const std::string server_address = "localhost:" + server.getPort().toString();
  std::cout << "Server address: " << server_address << "\n";

  Http::Client client;
  client.init();
  auto rb = client.get(server_address);
  auto response = rb.send();
  std::string resultData;
  response.then(
      [&resultData](Http::Response resp) {
        std::cout << "Response code is " << resp.code() << std::endl;
        if (resp.code() == Http::Code::Ok) {
          resultData = resp.body();
        }
      },
      Async::Throw);

  const int WAIT_TIME = 2;
  Async::Barrier<Http::Response> barrier(response);
  barrier.wait_for(std::chrono::seconds(WAIT_TIME));

  client.shutdown();
  server.shutdown();

  ASSERT_EQ("127.0.0.1", resultData);
}
