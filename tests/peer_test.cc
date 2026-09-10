/*
 * SPDX-FileCopyrightText: 2026 Pistache Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pistache/net.h>
#include <pistache/os.h>
#include <pistache/peer.h>
#include <pistache/winornix.h>

#include PST_SOCKET_HDR

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using namespace Pistache;
using namespace Pistache::Tcp;

namespace
{
    // Wraps a fresh, real socket fd in a Peer, mirroring the pattern
    // Listener::handleNewConnection uses to turn an accepted em_socket_t
    // into a Pistache Fd (see src/server/listener.cc).
    std::shared_ptr<Peer> makeTestPeer()
    {
        em_socket_t rawFd = PST_SOCK_SOCKET(AF_INET, SOCK_DGRAM, 0);
        EXPECT_NE(rawFd, static_cast<em_socket_t>(-1));

#ifdef _USE_LIBEVENT
        Fd fd = Polling::Epoll::em_event_new(
            rawFd, EVM_READ | EVM_WRITE | EVM_PERSIST | EVM_ET,
            F_SETFDL_NOTHING, F_SETFDL_NOTHING);
#else
        Fd fd = rawFd;
#endif

        return Peer::Create(fd, Address(Ipv4::any(), Port(0)));
    }
} // namespace

// fd_ used to be a plain Fd read/written without synchronization, so
// concurrent fd()/actualFd() (readers) and closeFd() (writer) - e.g. a
// handler thread still holding this Peer's shared_ptr while the
// transport's reactor thread closes it - could race. This is a
// best-effort stress test (proving the race needs something like TSan,
// which this sandboxed environment can't run), but it does exercise the
// exact concurrent access pattern the fix targets.
TEST(PeerTest, ConcurrentReadersDuringCloseDoNotCrash)
{
    auto peer = makeTestPeer();
    ASSERT_NE(peer->fd(), PS_FD_EMPTY);

    constexpr int ReaderThreads = 8;
    constexpr int Iterations    = 5000;

    std::vector<std::thread> readers;
    readers.reserve(ReaderThreads);
    for (int i = 0; i < ReaderThreads; ++i)
    {
        readers.emplace_back([&peer] {
            for (int j = 0; j < Iterations; ++j)
            {
                peer->fd();
                peer->actualFd();
            }
        });
    }

    peer->closeFd();

    for (auto& reader : readers)
        reader.join();

    EXPECT_EQ(peer->fd(), PS_FD_EMPTY);
    EXPECT_EQ(peer->actualFd(), -1);
}

// closeFd() used to read-then-clear fd_ as two separate steps, so two
// threads racing to close the same Peer could both observe the real fd and
// both attempt to close it. Concurrent calls should be safe regardless of
// how many threads call closeFd() at once.
TEST(PeerTest, CloseFdIsSafeUnderConcurrentCalls)
{
    auto peer = makeTestPeer();
    ASSERT_NE(peer->fd(), PS_FD_EMPTY);

    constexpr int ThreadCount = 8;
    std::vector<std::thread> closers;
    closers.reserve(ThreadCount);
    for (int i = 0; i < ThreadCount; ++i)
        closers.emplace_back([&peer] { peer->closeFd(); });

    for (auto& closer : closers)
        closer.join();

    EXPECT_EQ(peer->fd(), PS_FD_EMPTY);
    EXPECT_EQ(peer->actualFd(), -1);
}
