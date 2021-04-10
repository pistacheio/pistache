#pragma once

#include <pistache/net.h>
#include <pistache/os.h>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace Pistache
{

#define CLIENT_TRY(...)                   \
    do                                    \
    {                                     \
        auto ret = __VA_ARGS__;           \
        if (ret < 0)                      \
        {                                 \
            lastError_ = strerror(errno); \
            return false;                 \
        }                                 \
    } while (0)

    class TcpClient
    {
    public:
        bool connect(const Pistache::Address& address)
        {
            struct addrinfo hints;
            std::memset(&hints, 0, sizeof(hints));
            hints.ai_family   = address.family();
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags    = 0;
            hints.ai_protocol = 0;

            auto host = address.host();
            auto port = address.port().toString();

            AddrInfo addrInfo;
            CLIENT_TRY(addrInfo.invoke(host.c_str(), port.c_str(), &hints));

            const auto* addrs = addrInfo.get_info_ptr();
            int sfd           = -1;

            auto* addr = addrs;
            for (; addr; addr = addr->ai_next)
            {
                sfd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
                if (sfd < 0)
                    continue;

                break;
            }

            CLIENT_TRY(sfd);
            CLIENT_TRY(::connect(sfd, addr->ai_addr, addr->ai_addrlen));
            make_non_blocking(sfd);

            fd_ = sfd;
            return true;
        }

        bool send(const std::string& data)
        {
            return send(data.c_str(), data.size());
        }

        bool send(const void* data, size_t size)
        {
            CLIENT_TRY(::send(fd_, data, size, 0));
            return true;
        }

        template <typename Duration>
        bool receive(void* buffer, size_t size, size_t* bytes, Duration timeout)
        {
            struct pollfd fds[1];
            fds[0].fd     = fd_;
            fds[0].events = POLLIN;

            auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
            auto ret       = ::poll(fds, 1, static_cast<int>(timeoutMs.count()));

            if (ret < 0)
            {
                lastError_ = strerror(errno);
                return false;
            }
            if (ret == 0)
            {
                lastError_ = "Poll timeout";
                return false;
            }

            if (fds[0].revents & POLLERR)
            {
                lastError_ = "An error has occured on the stream";
                return false;
            }

            auto res = ::recv(fd_, buffer, size, 0);
            if (res < 0)
            {
                lastError_ = strerror(errno);
                return false;
            }

            *bytes = static_cast<size_t>(res);
            return true;
        }

        std::string lastError() const
        {
            return lastError_;
        }

    private:
        int fd_;
        std::string lastError_;
    };

#undef CLIENT_TRY

} // namespace Pistache
