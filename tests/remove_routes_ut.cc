#include <gtest/gtest.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/router.h>
#include <httplib.h>
#include "rapidjson/document.h"
#include <csignal>
#include <iostream>
#include <memory>

using namespace Pistache;
using namespace Rest;

static constexpr auto URL_1_MODE_1 = "/read/hello_fun_1_mode_1";
static constexpr auto URL_2_MODE_1 = "/read/hello_fun_2_mode_1";

static constexpr auto URL_1_MODE_2 = "/read/hello_fun_1_mode_2";
static constexpr auto URL_2_MODE_2 = "/read/hello_fun_2_mode_2";

class Server
{
public:
    enum Modes {
        mode_1,
        mode_2
    };
    Server(Address addr) :router_(std::make_shared<Router>()), 
    endpoint_(std::make_shared<Pistache::Http::Endpoint>(addr))
    {}
    void init(size_t thr = 2, Modes mode = Modes::mode_1){
        auto opts = Pistache::Http::Endpoint::options()
                        .threads(static_cast<int>(thr));
        endpoint_->init(opts);
        system_mode_ = mode;
        SetupRoutes();
    }
    virtual ~Server() = default;
    void SwitchMode()
    {
        if (system_mode_ == Modes::mode_1)
        {
            std::cout << "Setup remove route mode_1" <<  std::endl;
            Routes::Remove(*router_, Pistache::Http::Method::Get, URL_1_MODE_1);
            Routes::Remove(*router_, Pistache::Http::Method::Get, URL_2_MODE_1);
            system_mode_ = Modes::mode_2;      
        }
        else
        {
            std::cout << "Setup remove route mode_2" <<  std::endl;
            Routes::Remove(*router_, Pistache::Http::Method::Get, URL_1_MODE_2);
            Routes::Remove(*router_, Pistache::Http::Method::Get, URL_2_MODE_2);
            system_mode_ = Modes::mode_1;
        }
        SetupRoutes();
    }
    void StartServer()
    {
        endpoint_->setHandler(router_->handler(router_));
        endpoint_->serveThreaded();
    }
    void StopServer()
    {
        endpoint_->shutdown();
    }
    Port getPort()
    {
        return endpoint_->getPort();

    }

private:
    std::shared_ptr<Router> router_;
   
    std::shared_ptr<Http::Endpoint> endpoint_;

private:
    void SetupRoutes()
    {
        using namespace Pistache::Rest;
        if (system_mode_ == Modes::mode_1)
        {
            std::cout << "Setup add route mode_1" <<  std::endl;
            Routes::Get(*router_, URL_1_MODE_1, Routes::bind(&Server::hello_fun_1_mode_1, this));
            Routes::Get(*router_, URL_2_MODE_1, Routes::bind(&Server::hello_fun_2_mode_1, this));
        }
        else
        {
            std::cout << "Setup add route mode_2" <<  std::endl;
            Routes::Get(*router_, URL_1_MODE_2, Routes::bind(&Server::hello_fun_2_mode_2, this));
            Routes::Get(*router_, URL_2_MODE_2, Routes::bind(&Server::hello_fun_2_mode_2, this));
        }
    }

    void hello_fun_1_mode_1(const Rest::Request& /*request*/, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, response.peer()->hostname());
    }
    void hello_fun_2_mode_1(const Rest::Request& /*request*/, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, response.peer()->hostname());
    }
    void hello_fun_1_mode_2(const Rest::Request& /*request*/, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, response.peer()->hostname());
    }
    void hello_fun_2_mode_2(const Rest::Request& /*request*/, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, response.peer()->hostname());
    }
    Modes system_mode_;
};

TEST(rest_server_test, remove_routes_crash)
{
    Address addr(Ipv4::any(), Port(0));
    auto server_instance = std::make_unique<Server>(addr);
    server_instance->init();
    server_instance->StartServer();
    Port port = server_instance->getPort();
    httplib::Client client("localhost", port);
    //server should expose mode 1 routes
    auto res = client.Get(URL_1_MODE_1);
    EXPECT_EQ(res->status, 200);
    res = client.Get(URL_2_MODE_1);
    EXPECT_EQ(res->status, 200);

    res = client.Get(URL_1_MODE_2);
    EXPECT_EQ(res->status, 404);
    res = client.Get(URL_2_MODE_2);
    EXPECT_EQ(res->status, 404);

    server_instance->SwitchMode();
    //server should expose mode 2 routes
    res = client.Get(URL_1_MODE_2);
    EXPECT_EQ(res->status, 200);
    res = client.Get(URL_2_MODE_2);
    EXPECT_EQ(res->status, 200);

    res = client.Get(URL_1_MODE_1);
    EXPECT_EQ(res->status, 404);
    res = client.Get(URL_2_MODE_1);
    EXPECT_EQ(res->status, 404);

    server_instance->StopServer();
}
