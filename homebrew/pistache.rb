#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

class Pistache < Formula
  desc "Modern, fast, elegant HTTP + REST C++17 framework with pleasant API"
  homepage "https://github.com/pistacheio/pistache"
  # !!!!!!!! CHANGE THIS to a https://github.com/pistacheio release
  url "https://github.com/dgreatwood/pistachefork/archive/refs/tags/v0.4.20.tar.gz"
  sha256 "185b31a62d9bbb0f0a04e2768c62415bafb9037e62826ecd5fb4edaf1ba44d0b"
  license "Apache-2.0"
  head "https://github.com/pistacheio/pistache.git", branch: "master"

  depends_on "brotli" => :build
  depends_on "cmake" => :build # Required so meson finds hinnant-date
  depends_on "cpp-httplib" => :build
  depends_on "googletest" => :build
  depends_on "howard-hinnant-date" => :build
  depends_on "libevent" => :build
  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkg-config" => :build
  depends_on "rapidjson" => :build
  depends_on "zstd" => :build

  def install
    system "meson", "setup", "build",
           "--buildtype=release",
           "-DPISTACHE_USE_SSL=true", "-DPISTACHE_BUILD_EXAMPLES=true",
           "-DPISTACHE_BUILD_TESTS=true",
           "-DPISTACHE_BUILD_DOCS=false",
           "-DPISTACHE_USE_CONTENT_ENCODING_DEFLATE=true",
           "-DPISTACHE_USE_CONTENT_ENCODING_BROTLI=true",
           "-DPISTACHE_USE_CONTENT_ENCODING_ZSTD=true",
           *std_meson_args

    system "meson", "compile", "-C", "build", "--verbose"
    system "meson", "install", "-C", "build"
  end

  test do
    (testpath/"test.cpp").write <<~CPP
      // Testing multiple clients making requests of a multithreaded server

      #include <pistache/async.h>
      #include <pistache/client.h>
      #include <pistache/endpoint.h>
      #include <pistache/http.h>

      #include <chrono>
      #include <string>
      #include <future>

      using namespace Pistache;
      using namespace std::chrono;

      struct HelloHandler : public Http::Handler
      {
          HTTP_PROTOTYPE(HelloHandler)

          void onRequest(const Http::Request& /*request*/,
                         Http::ResponseWriter writer) override
          {
              writer.send(Http::Code::Ok, "Hello, World!");
          }
      };

      static int clientLogicFunc(size_t response_size,
                                 const std::string& server_page,
                                 int wait_seconds)
      {
          Http::Experimental::Client client;
          client.init();

          std::vector<Async::Promise<Http::Response>> responses;
          auto rb = client.get(server_page);

          int resolver_counter = 0;
          int reject_counter   = 0;
          for (size_t i = 0; i < response_size; ++i)
          {
              auto response = rb.send();

              response.then(
                  [&resolver_counter, pos = i](Http::Response resp) {
                      if (resp.code() == Http::Code::Ok)
                      {
                          ++resolver_counter;
                      }
                  },
                  [&reject_counter, pos = i](std::exception_ptr exc) {
                      std::cout << "Request rejected" << std::endl;
                      PrintException excPrinter;

                      excPrinter(exc);
                      ++reject_counter;
                  });
              responses.push_back(std::move(response));
          }

          { // encapsulate
              auto sync = Async::whenAll(responses.begin(), responses.end());
              Async::Barrier<std::vector<Http::Response>> barrier(sync);
              barrier.wait_for(std::chrono::seconds(wait_seconds));
          }

          client.shutdown();
          return resolver_counter;
      }

      int main()
      {
          const Pistache::Address address("localhost", Pistache::Port(0));

          Http::Endpoint server(address);
          auto flags       = Tcp::Options::ReuseAddr;
          auto server_opts = Http::Endpoint::options().flags(flags).threads(3);
          server.init(server_opts);
          server.setHandler(Http::make_handler<HelloHandler>());
          server.serveThreaded();

          const std::string server_address =
              "localhost:" + server.getPort().toString();

          const int SIX_SECONDS_TIMOUT        = 6;
          const int FIRST_CLIENT_REQUEST_SIZE = 4;
          std::future<int> result1(std::async(clientLogicFunc,
                                   FIRST_CLIENT_REQUEST_SIZE, server_address,
                                   SIX_SECONDS_TIMOUT));
          const int SECOND_CLIENT_REQUEST_SIZE = 5;
          std::future<int> result2(
              std::async(clientLogicFunc, SECOND_CLIENT_REQUEST_SIZE,
                         server_address, SIX_SECONDS_TIMOUT));

          int res1 = result1.get();
          int res2 = result2.get();

          server.shutdown();

          if (res1 != FIRST_CLIENT_REQUEST_SIZE)
          {
              std::cerr << "Response count res1 is " << res1 << ", expected "
                        << FIRST_CLIENT_REQUEST_SIZE << std::endl;
              return 1;
          }

          if (res2 != SECOND_CLIENT_REQUEST_SIZE)
          {
              std::cerr << "Response count res2 is " << res2 << ", expected "
                        << SECOND_CLIENT_REQUEST_SIZE << std::endl;
              return 2;
          }

        return 0;
      }
    CPP
    system ENV.cxx, "-std=c++17", "test.cpp", "-L#{lib}", "-lpistache", "-o", "test"
    system "./test"
  end
end
