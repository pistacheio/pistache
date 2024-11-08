/*
 * SPDX-FileCopyrightText: 2019 Oleg Burchakov
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pistache/endpoint.h"
#include <signal.h>

#ifdef _WIN32
#include <Windows.h>
#include <WinCon.h>
// #include <ConsoleApi.h> // included in WinCon.h; needs Kernel32.lib
#endif

using namespace Pistache;

class HelloHandler : public Http::Handler
{
public:
    HTTP_PROTOTYPE(HelloHandler)

    void onRequest(const Http::Request& /*request*/, Http::ResponseWriter response) override
    {
        response.send(Pistache::Http::Code::Ok, "Hello World\n");
    }
};

#ifdef _WIN32

static std::atomic<bool> lSigBool = false;
static std::mutex lMutex;
static std::condition_variable cv;

#define CTRL_TYPE_EMPTY 0xDEADDEAD
static DWORD lCtrlType = CTRL_TYPE_EMPTY;
static BOOL consoleCtrlHandler(DWORD dwCtrlType)
{
    std::unique_lock<std::mutex> lock(lMutex);
    if (lCtrlType == 0)
    {
        lCtrlType = dwCtrlType;

        lSigBool = true;
        cv.notify_one();
    }

    return(TRUE);// We have handled the ctrl signal
}

#endif // of ifdef _WIN32

int main()
{
#ifdef _WIN32
    // Note: SetConsoleCtrlHandler can be used for console apps or GUI apps;
    // for GUI apps, the notification from WM_QUERYENDSESSION _may_ arrive
    // before the call to consoleCtrlHandler

    BOOL set_cch_res = SetConsoleCtrlHandler(consoleCtrlHandler,
                                             true /*Add*/);
    if (!set_cch_res)
    {
        perror("install ctrl-c-handler failed");
        return 1;
    }

#else
    sigset_t signals;
    if (sigemptyset(&signals) != 0
        || sigaddset(&signals, SIGTERM) != 0
        || sigaddset(&signals, SIGINT) != 0
        || sigaddset(&signals, SIGHUP) != 0
        || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0)
    {
        perror("install signal handler failed");
        return 1;
    }
#endif

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(9080));
    auto opts = Pistache::Http::Endpoint::options()
                    .threads(1);

    Http::Endpoint server(addr);
    server.init(opts);
    server.setHandler(Http::make_handler<HelloHandler>());
    server.serveThreaded();

#ifdef _WIN32
    std::unique_lock<std::mutex> lock(lMutex);
    cv.wait(lock, [&] {return(lSigBool.load());});

    switch(lCtrlType)
    {
    case CTRL_TYPE_EMPTY:
        perror("ctrl-type not set");
        break;

    case CTRL_C_EVENT:
        std::cout <<
            "ctrl-c received from keyboard or GenerateConsoleCtrlEvent" <<
            std::endl;
        break;

    case CTRL_BREAK_EVENT:
        std::cout <<
            "ctrl-break received from keyboard or GenerateConsoleCtrlEvent" <<
            std::endl;
        break;

    case CTRL_CLOSE_EVENT:
        std::cout <<
            "Attached console closed" << std::endl;
        break;

    case CTRL_LOGOFF_EVENT:
        std::cout <<
            "User logging off" << std::endl;
        break;

    case CTRL_SHUTDOWN_EVENT:
        std::cout <<
            "System shutting down" << std::endl;
        break;

    default:
        perror("ctrl-type unknown");
        break;
    }
#else // not ifdef _WIN32
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }
#endif // of ifdef _WIN32... else...

    server.shutdown();
}
