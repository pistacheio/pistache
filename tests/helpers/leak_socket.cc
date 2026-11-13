/*
 * SPDX-FileCopyrightText: 2026 Pistache Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pistache/listener.h>
#include <pistache/http.h>

#include <cstdlib>
#include <string>

class DummyHandler : public Pistache::Http::Handler
{
public:
    HTTP_PROTOTYPE(DummyHandler)

    void onRequest(const Pistache::Http::Request&,
                   Pistache::Http::ResponseWriter response) override
    {
        response.send(Pistache::Http::Code::Ok);
    }
};

int main(int argc, char* argv[])
{
    if (argc != 3)
        return 2;

    const auto port = static_cast<uint16_t>(std::stoul(argv[1]));
    auto options = Pistache::Tcp::Options::ReuseAddr;
    if (std::string(argv[2]) == "1")
        options = options | Pistache::Tcp::Options::CloseOnExec;

    Pistache::Tcp::Listener listener(
        Pistache::Address(Pistache::Ipv4::any(), Pistache::Port(port)));
    listener.setHandler(Pistache::Http::make_handler<DummyHandler>());
    listener.init(1, Pistache::Flags<Pistache::Tcp::Options>(options));
    listener.bind();

    // The background process keeps the socket open unless it has FD_CLOEXEC.
    return std::system("sleep 10 <&- &");
}
