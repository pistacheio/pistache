/*
 * SPDX-FileCopyrightText: 2020 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pistache/winornix.h>
#include <pistache/net.h>
#include <pistache/os.h>

#include PIST_QUOTE(PST_NETDB_HDR)
#include PIST_QUOTE(PIST_POLL_HDR)
#include PIST_QUOTE(PST_SOCKET_HDR) // best in C/C++, not .h, for non-test code

#include <sys/types.h>

// In CLIENT_TRY, note that strerror is allowed to change errno in certain
// circumstances, so we must save errno in lastErrno_ BEFORE we call strerror
//
// Secondly, if errno has not been set at all then we set lastErrno_ =
// ECANCELED; the ECANCELED errno is not used in Pistache code as of Aug/2024.
static const char * strerror_errstr = "<no strerror>";
namespace Pistache
{
#define CLIENT_TRY(...)                                                 \
    do                                                                  \
    {                                                                   \
        auto ret = __VA_ARGS__;                                         \
        if (ret == -1)                                                  \
        {                                                               \
            if (errno)                                                  \
            {                                                           \
                lastErrno_ = errno;                                     \
                PST_DECL_SE_ERR_P_EXTRA;                                  \
                const char * se = PST_STRERROR_R_ERRNO; \
                if (se)                                                 \
                    lastError_ = std::string(se);                       \
                if (errno != lastErrno_)                                \
                    std::cout << "strerror changed errno (was " <<      \
                        lastErrno_ << ", now " << errno << " )" << std::endl; \
                                                                        \
                if (lastError_.empty())                                 \
                    lastError_ = strerror_errstr;                       \
            }                                                           \
            else                                                        \
            {                                                           \
                std::cout << "ret is -1, but errno not set" << std::endl; \
                if (!lastErrno_)                                        \
                {                                                       \
                    std::cout << "Setting lastErrno_ to ECANCELED" <<   \
                        std::endl;                                      \
                    lastError_ = strerror_errstr;                       \
                    lastErrno_ = ECANCELED;                             \
                }                                                       \
            }                                                           \
            return false;                                               \
        }                                                               \
    } while (0)

    class TcpClient
    {
    public:
        bool connect(const Pistache::Address& address)
        {
            struct addrinfo hints = {};
            hints.ai_family       = address.family();
            hints.ai_socktype     = SOCK_STREAM;

            auto host = address.host();
            auto port = address.port().toString();

            AddrInfo addrInfo;
            CLIENT_TRY(addrInfo.invoke(host.c_str(), port.c_str(), &hints));

            const auto* addrs = addrInfo.get_info_ptr();
            em_socket_t sfd   = -1;

            auto* addr = addrs;
            for (; addr; addr = addr->ai_next)
            {
                sfd = PST_SOCK_SOCKET(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
                PS_LOG_DEBUG_ARGS("::socket actual_fd %d", sfd);

                if (sfd < 0)
                    continue;

                break;
            }

            CLIENT_TRY(sfd);
            CLIENT_TRY(PST_SOCK_CONNECT(sfd, addr->ai_addr,
                                        static_cast<PST_SOCKLEN_T>(addr->ai_addrlen)));
            make_non_blocking(sfd);

            fd_ = sfd;
            return true;
        }

        bool send(const std::string& data)
        {
            return send(data.c_str(), data.size());
        }

        bool send(const char* data, size_t len)
        {
            size_t total = 0;
            while (total < len)
            {
                int send_flags =
#ifdef _IS_WINDOWS
                    0;
#else
                MSG_NOSIGNAL; // Doesn't exist in Windows
#endif
                PST_SSIZE_T n = PST_SOCK_SEND(fd_, data + total, len - total, send_flags);
                if (n == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    else
                    {
                        CLIENT_TRY(n);
                    }
                }
                else
                {
                    total += static_cast<size_t>(n);
                }
            }
            return true;
        }

        template <typename Duration>
        bool receive(void* buffer, size_t size, size_t* bytes, Duration timeout)
        {
            struct PST_POLLFD fds[1];
            fds[0].fd     = fd_;
            fds[0].events = POLLIN;

            auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
            auto ret       = PST_SOCK_POLL(fds, 1, static_cast<int>(timeoutMs.count()));

            if (ret < 0)
            {
                PST_DECL_SE_ERR_P_EXTRA;
                const char * se = PST_STRERROR_R_ERRNO;
                if (se)
                    lastError_ = std::string(se);
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

            auto res = PST_SOCK_READ(fd_, buffer, size);
            if (res < 0)
            {
                PST_DECL_SE_ERR_P_EXTRA;
                const char * se = PST_STRERROR_R_ERRNO;
                if (se)
                    lastError_ = std::string(se);
                return false;
            }

            *bytes = static_cast<size_t>(res);
            return true;
        }

        std::string lastError() const
        {
            return lastError_;
        }

        int lastErrno() const
        {
            return lastErrno_;
        }

    private:
        em_socket_t fd_ = -1;
        std::string lastError_;
        int lastErrno_ = 0;
    };

#undef CLIENT_TRY

} // namespace Pistache
