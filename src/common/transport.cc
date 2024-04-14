/*
 * SPDX-FileCopyrightText: 2017 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* traqnsport.cc
   Mathieu Stefani, 02 July 2017

   TCP transport handling

*/

#include <pistache/eventmeth.h>

#ifdef _USE_LIBEVENT_LIKE_APPLE
// For sendfile(...) function
#include <sys/types.h>
#include <sys/uio.h>

#include <netinet/tcp.h> // for TCP_NOPUSH
#endif

#ifdef __linux__
#include <sys/sendfile.h>
#endif

#ifndef _USE_LIBEVENT_LIKE_APPLE
// Note: sys/timerfd.h is linux-only (and certainly POSIX only)
#include <sys/timerfd.h>
#endif

#include <pistache/os.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>
#include <pistache/transport.h>
#include <pistache/utils.h>

using std::to_string;

namespace Pistache::Tcp
{
    using namespace Polling;

    Transport::Transport(const std::shared_ptr<Tcp::Handler>& handler)
#ifdef _USE_LIBEVENT_LIKE_APPLE
        : tcp_prot_num_(-1)
#endif
    {
        init(handler);
    }

    void Transport::init(const std::shared_ptr<Tcp::Handler>& handler)
    {
        // Note: In the _USE_LIBEVENT case, EventMeth::init() called out of
        // EventMethFns::create

        handler_ = handler;
        handler_->associateTransport(this);

#ifdef _USE_LIBEVENT_LIKE_APPLE
        // SOL_TCP not defined in macOS Nov 2023
        const struct protoent* pe = getprotobyname("tcp");

        // TCP protocol num on this host
        tcp_prot_num_ = pe ? pe->p_proto : 6; // it's usually 6...
#endif
    }

    Transport::~Transport()
    {
        removeAllPeers();
    }

    std::shared_ptr<Aio::Handler> Transport::clone() const
    {
        return std::make_shared<Transport>(handler_->clone());
    }

    void Transport::flush()
    {
        handleWriteQueue(true);
    }

    void Transport::registerPoller(Polling::Epoll& poller)
    {
        PS_TIMEDBG_START_THIS;

        writesQueue.bind(poller);
        timersQueue.bind(poller);
        peersQueue.bind(poller);
        notifier.bind(poller);

#ifdef _USE_LIBEVENT
        epoll_fd = poller.getEventMethEpollEquiv();
#endif
    }

    void Transport::unregisterPoller(Polling::Epoll& poller)
    {
        PS_TIMEDBG_START_THIS;

#ifdef _USE_LIBEVENT
        epoll_fd = NULL;
#endif

        notifier.unbind(poller);
        peersQueue.unbind(poller);
        timersQueue.unbind(poller);
        writesQueue.unbind(poller);
    }

    void Transport::handleNewPeer(const std::shared_ptr<Tcp::Peer>& peer)
    {
        auto ctx                   = context();
        const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
        if (!isInRightThread)
        {
            PeerEntry entry(peer);
            peersQueue.push(std::move(entry));
        }
        else
        {
            handlePeer(peer);
        }
        Fd fd = peer->fd();
        {
            Guard guard(toWriteLock);
            toWrite.emplace(fd, std::deque<WriteEntry> {});
        }
    }

#ifdef DEBUG
    static void logFdAndNotifyOn(const Aio::FdSet::Entry& entry)
    {
#ifdef _USE_LIBEVENT
        std::string str("entry");
#else
        std::string str("fd ");

        std::stringstream ss;
        ss << ((Fd)entry.getTag().value());
        str += ss.str();
#endif

        const char* flag_str = NULL;

        if (entry.isReadable())
            flag_str = " readable";
        if (entry.isWritable())
            flag_str = " writable";
        if (entry.isHangup())
            flag_str = " hangup";
        if (!flag_str)
            flag_str = " <unknown>";
        str += flag_str;

        PS_LOG_DEBUG_ARGS("%s", str.c_str());
    }

#define PS_LOG_DBG_FD_AND_NOTIFY logFdAndNotifyOn(entry)
#else
#define PS_LOG_DBG_FD_AND_NOTIFY
#endif

    void Transport::onReady(const Aio::FdSet& fds)
    {
        PS_LOG_DEBUG_ARGS("%d fds", fds.size());

        for (const auto& entry : fds)
        {
            PS_LOG_DBG_FD_AND_NOTIFY;

            if (entry.getTag() == writesQueue.tag())
            {
                PS_LOG_DEBUG("Write queue");
                handleWriteQueue();
            }
            else if (entry.getTag() == timersQueue.tag())
            {
                PS_LOG_DEBUG("Timers queue");
                handleTimerQueue();
            }
            else if (entry.getTag() == peersQueue.tag())
            {
                PS_LOG_DEBUG("Peers queue");
                handlePeerQueue();
            }
            else if (entry.getTag() == notifier.tag())
            {
                PS_LOG_DEBUG("notifier");
                handleNotify();
            }

            else if (entry.isReadable())
            {
                auto tag = entry.getTag();
                PS_LOG_DEBUG_ARGS("entry isReadable fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                                  ((Fd)tag.value()));

                if (isPeerFd(tag))
                {
                    auto& peer = getPeer(tag);
                    PS_LOG_DEBUG("handleIncoming");
                    handleIncoming(peer);
                }
                else if (isTimerFd(tag))
                {
                    auto it      = timers.find(static_cast<decltype(timers)::key_type>(tag.value()));
                    auto& entry_ = it->second;
                    PS_LOG_DEBUG("handleTimer");
                    handleTimer(std::move(entry_));
                    PS_LOG_DEBUG_ARGS("Timer %" PIST_QUOTE(PS_FD_PRNTFCD) " erased from timers",
                                      it->first);

                    PS_LOG_DEBUG_ARGS("Timer %" PIST_QUOTE(PS_FD_PRNTFCD) " erasing from timers",
                                      it->first);
                    timers.erase(it->first);
                }
                else
                {
                    PS_LOG_DEBUG("neither peer nor timer");
                }
            }
            else if (entry.isWritable())
            {
                PS_LOG_DEBUG("isWritable");

                auto tag        = entry.getTag();
                FdConst fdconst = static_cast<FdConst>(tag.value());
                // Since fd is about to be written to, it isn't really const,
                // and we cast away the const
                Fd fd = ((Fd)fdconst);

                {
                    Guard guard(toWriteLock);
                    auto it = toWrite.find(fd);
                    if (it == std::end(toWrite))
                    {
                        throw std::runtime_error(
                            "Assertion Error: could not find write data");
                    }
                }

                reactor()->modifyFd(key(), fd, NotifyOn::Read, Polling::Mode::Edge);

                PS_LOG_DEBUG("asyncWriteImpl (drain queue)");
                // Try to drain the queue
                asyncWriteImpl(fd);
            }
        }
    }

    void Transport::disarmTimer(Fd fd)
    {
        PS_TIMEDBG_START_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

        auto it = timers.find(fd);
        if (it == std::end(timers))
            throw std::runtime_error("Timer has not been armed");

        auto& entry = it->second;
        entry.disable();
    }

    void Transport::handleIncoming(const std::shared_ptr<Peer>& peer)
    {
        char buffer[Const::MaxBuffer] = { 0 };

        ssize_t totalBytes = 0;
        int fdactual       = GET_ACTUAL_FD(peer->fd());

        for (;;)
        {

            ssize_t bytes;

#ifdef PISTACHE_USE_SSL
            if (peer->ssl() != NULL)
            {
                PS_LOG_DEBUG("SSL_read");

                bytes = SSL_read((SSL*)peer->ssl(), buffer + totalBytes,
                                 static_cast<int>(Const::MaxBuffer - totalBytes));
            }
            else
            {
#endif /* PISTACHE_USE_SSL */
                PS_LOG_DEBUG("recv (read)");
                bytes = recv(fdactual, buffer + totalBytes, Const::MaxBuffer - totalBytes, 0);
#ifdef PISTACHE_USE_SSL
            }
#endif /* PISTACHE_USE_SSL */

            PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", "
                                                               "bytes read %d, totalBytes %d, "
                                                               "err %d %s",
                              peer->fd(), bytes, totalBytes,
                              (bytes < 0) ? errno : 0,
                              (bytes < 0) ? strerror(errno) : "");

            if (bytes == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (totalBytes > 0)
                    {
                        handler_->onInput(buffer, totalBytes, peer);
                    }
                }
                else
                {
                    handlePeerDisconnection(peer);
                }
                break;
            }
            else if (bytes == 0)
            {
                handlePeerDisconnection(peer);
                break;
            }

            else
            {
                handler_->onInput(buffer, bytes, peer);
            }
        }
    }

    void Transport::handlePeerDisconnection(const std::shared_ptr<Peer>& peer)
    {
        handler_->onDisconnection(peer);

        removePeer(peer);
    }

    void Transport::removePeer(const std::shared_ptr<Peer>& peer)
    {
        Fd fd = peer->fd();

        {
            // See comment in transport.h on why peers_ must be mutex-protected
            std::lock_guard<std::mutex> l_guard(peers_mutex_);

            auto it = peers_.find(fd);
            if (it == std::end(peers_))
            {
                PS_LOG_WARNING_ARGS("peer %p not found in peers_", peer.get());
            }
            else
            {
                peers_.erase(it);
            }
        }

        {
            // Clean up buffers
            Guard guard(toWriteLock);
            toWrite.erase(fd);
        }

        // Don't rely on close deleting this FD from the epoll "interest" list.
        // This is needed in case the FD has been shared with another process.
        // Sharing should no longer happen by accident as SOCK_CLOEXEC is now set on
        // listener accept. This should then guarantee that the next call to
        // epoll_wait will not give us any events relating to this FD even if they
        // have been queued in the kernel since the last call to epoll_wait.
        Aio::Reactor* r = reactor();
        if (r) // or if r is NULL then reactor has been detached already
            r->removeFd(key(), fd);

        CLOSE_FD(fd);
    }

    void Transport::removeAllPeers()
    {
        for (;;)
        {
            std::shared_ptr<Peer> peer;

            { // encapsulate
                std::lock_guard<std::mutex> l_guard(peers_mutex_);
                auto it = peers_.begin();
                if (it == peers_.end())
                    break;

                peer = it->second;
                if (!peer)
                {
                    peers_.erase(it);
                    PS_LOG_DEBUG("peer NULL");
                    continue;
                }
            }

            removePeer(peer); // Note: removePeer locks mutex
        }
    }

    void Transport::asyncWriteImpl(Fd fd)
    {
        PS_TIMEDBG_START_THIS;

        bool stop = false;
        while (!stop)
        {
            std::unique_lock<std::mutex> lock(toWriteLock);

            auto it = toWrite.find(fd);

            // cleanup will have been handled by handlePeerDisconnection
            if (it == std::end(toWrite))
            {
                PS_LOG_DEBUG_ARGS("Failed to find fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);
                return;
            }
            auto& wq = it->second;
            if (wq.empty())
            {
                PS_LOG_DEBUG("wq empty");
                break;
            }

            auto& entry = wq.front();
            int flags   = entry.flags;
#ifdef _USE_LIBEVENT_LIKE_APPLE
            bool msg_more_style = entry.msg_more_style;
#endif
            BufferHolder& buffer              = entry.buffer;
            Async::Deferred<ssize_t> deferred = std::move(entry.deferred);

            auto cleanUp = [&]() {
                wq.pop_front();
                if (wq.empty())
                {
                    PS_LOG_DEBUG_ARGS("Erasing fd %" PIST_QUOTE(PS_FD_PRNTFCD) " from toWrite", fd);
                    toWrite.erase(fd);
                    reactor()->modifyFd(key(), fd, NotifyOn::Read, Polling::Mode::Edge);
                    stop = true;
                }
                lock.unlock();
            };

            size_t totalWritten = buffer.offset();
            for (;;)
            {
                ssize_t bytesWritten = 0;
                auto len             = buffer.size() - totalWritten;

                if (buffer.isRaw())
                {
                    PS_LOG_DEBUG_ARGS("sendRawBuffer fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", len %d",
                                      fd, len);

                    auto raw        = buffer.raw();
                    const auto* ptr = raw.data().c_str() + totalWritten;
                    bytesWritten    = sendRawBuffer(fd, ptr, len, flags
#ifdef _USE_LIBEVENT_LIKE_APPLE
                                                 ,
                                                 msg_more_style
#endif
                    );
                }
                else
                {
                    PS_LOG_DEBUG_ARGS("sendFile fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", len %d",
                                      fd, len);

                    auto file    = buffer.fd();
                    off_t offset = totalWritten;
                    bytesWritten = sendFile(fd, file, offset, len);
                }
                if (bytesWritten < 0)
                {
                    PS_LOG_DEBUG_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD) " errno %d %s",
                                      fd, errno, strerror(errno));

                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        auto bufferHolder = buffer.detach(totalWritten);

                        // pop_front kills buffer - so we cannot continue loop or use buffer
                        // after this point
                        wq.pop_front();
                        wq.push_front(WriteEntry(std::move(deferred),
                                                 bufferHolder, fd, flags
#ifdef _USE_LIBEVENT_LIKE_APPLE
                                                 ,
                                                 msg_more_style
#endif
                                                 ));
                        reactor()->modifyFd(key(), fd, NotifyOn::Read | NotifyOn::Write,
                                            Polling::Mode::Edge);
                    }
                    // EBADF can happen when the HTTP parser, in the case of
                    // an error, closes fd before the entire request is processed.
                    // https://github.com/pistacheio/pistache/issues/501
                    else if (errno == EBADF || errno == EPIPE || errno == ECONNRESET)
                    {
                        PS_LOG_DEBUG_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD) " EBADF/EPIPE/ECONNRESET so erasing",
                                          fd);
                        wq.pop_front();
                        toWrite.erase(fd);
                        stop = true;
                    }
                    else
                    {
                        PS_LOG_DEBUG_ARGS("fd %" PIST_QUOTE(PS_FD_PRNTFCD) " rejecting write attempt", fd);
                        cleanUp();
                        deferred.reject(Pistache::Error::system("Could not write data"));
                    }
                    break;
                }
                else
                {
                    totalWritten += bytesWritten;
                    if (totalWritten >= buffer.size())
                    {
                        if (buffer.isFile())
                        {
                            PS_LOG_DEBUG_ARGS("file ::close actual_fd %d",
                                              buffer.fd());

                            // done with the file buffer, nothing else knows
                            // whether to close it with the way the code is
                            // written.
                            ::close(buffer.fd());
                        }

                        cleanUp();

                        // Cast to match the type of defered template
                        // to avoid a BadType exception
                        deferred.resolve(static_cast<ssize_t>(totalWritten));
                        break;
                    }
                }
            }
        }
    }

#ifdef _USE_LIBEVENT_LIKE_APPLE
    void Transport::configureMsgMoreStyle(Fd fd, bool msg_more_style)
    {
        int tcp_no_push  = 0;
        socklen_t len    = sizeof(tcp_no_push);
        int sock_opt_res = getsockopt(GET_ACTUAL_FD(fd), tcp_prot_num_,
#ifdef __APPLE__
                                      TCP_NOPUSH,
#else
                                      TCP_CORK,
#endif
                                      &tcp_no_push, &len);
        if (sock_opt_res == 0)
        {
            if (((tcp_no_push == 0) && (msg_more_style)) || ((tcp_no_push != 0) && (!msg_more_style)))
            {
                PS_LOG_DEBUG_ARGS("Setting MSG_MORE style to %s",
                                  (msg_more_style) ? "on" : "off");

                tcp_no_push  = msg_more_style ? 1 : 0;
                sock_opt_res = setsockopt(GET_ACTUAL_FD(fd), tcp_prot_num_,
#ifdef __APPLE__
                                          TCP_NOPUSH,
#else
                                          TCP_CORK,
#endif
                                          &tcp_no_push, len);
                if (sock_opt_res < 0)
                    throw std::runtime_error("setsockopt failed");
            }
#ifdef DEBUG
            else
            {
                PS_LOG_DEBUG_ARGS("MSG_MORE style is already %s",
                                  (tcp_no_push) ? "on" : "off");
            }
#endif
        }
#ifdef DEBUG
        else
        {
            PS_LOG_DEBUG_ARGS("getsockopt failed for fd %p, actual fd %d",
                              fd, GET_ACTUAL_FD(fd));
            throw std::runtime_error("getsockopt failed");
        }
#endif
    }
#endif // of ifdef _USE_LIBEVENT_LIKE_APPLE

    ssize_t Transport::sendRawBuffer(Fd fd, const char* buffer, size_t len,
                                     int flags
#ifdef _USE_LIBEVENT_LIKE_APPLE
                                     ,
                                     bool msg_more_style
#endif
    )
    {
        ssize_t bytesWritten = 0;

#ifdef PISTACHE_USE_SSL
        bool it_second_ssl_is_null = false;

        {
            // See comment in transport.h on why peers_ must be mutex-protected
            std::lock_guard<std::mutex> l_guard(peers_mutex_);

            auto it_ = peers_.find(fd);

            if (it_ == std::end(peers_))
                throw std::runtime_error(
                    "No peer found for fd: " + to_string(fd));

            it_second_ssl_is_null = (it_->second->ssl() == NULL);

            if (!it_second_ssl_is_null)
            {
                auto ssl_ = static_cast<SSL*>(it_->second->ssl());
                PS_LOG_DEBUG_ARGS("SSL_write, len %d", (int)len);

                bytesWritten = SSL_write(ssl_, buffer, static_cast<int>(len));
            }
        }

        if (it_second_ssl_is_null)
        {
#endif /* PISTACHE_USE_SSL */
            // MSG_NOSIGNAL is used to prevent SIGPIPE on client connection termination

#ifdef _USE_LIBEVENT_LIKE_APPLE
            configureMsgMoreStyle(fd, msg_more_style);
#endif

            PS_LOG_DEBUG_ARGS("::send, fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", actual_fd %d, len %d",
                              fd, GET_ACTUAL_FD(fd), (int)len);

            bytesWritten = ::send(GET_ACTUAL_FD(fd), buffer, len,
                                  flags | MSG_NOSIGNAL);

            PS_LOG_DEBUG_ARGS("bytesWritten = %d", bytesWritten);

#ifdef PISTACHE_USE_SSL
        }
#endif /* PISTACHE_USE_SSL */

        return bytesWritten;
    }

    ssize_t Transport::sendFile(Fd fd, int file, off_t offset, size_t len)
    {
        ssize_t bytesWritten = 0;

#ifdef PISTACHE_USE_SSL
        bool it_second_ssl_is_null = false;

        {
            // See comment in transport.h on why peers_ must be mutex-protected
            std::lock_guard<std::mutex> l_guard(peers_mutex_);
            auto it_ = peers_.find(fd);

            if (it_ == std::end(peers_))
            {
                PS_LOG_WARNING_ARGS("No peer for fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);
                PS_LOG_WARNING_ARGS("No peer found for fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", actual-fd %d",
                                    fd, GET_ACTUAL_FD(fd));

                throw std::runtime_error(
                    "No peer found for fd: " + to_string(fd));
            }
            it_second_ssl_is_null = (it_->second->ssl() == NULL);

            if (!it_second_ssl_is_null)
            {
                PS_LOG_DEBUG_ARGS("SSL_sendfile, len %d", len);

                auto ssl_    = static_cast<SSL*>(it_->second->ssl());
                bytesWritten = SSL_sendfile(ssl_, file, &offset, len);
            }
        }

        if (it_second_ssl_is_null)
        {
#endif /* PISTACHE_USE_SSL */
            PS_LOG_DEBUG_ARGS("::sendfile fd %" PIST_QUOTE(PS_FD_PRNTFCD) " actual-fd %d, file fd, len %d",
                              fd, GET_ACTUAL_FD(fd), file, len);

#ifdef _USE_LIBEVENT_LIKE_APPLE
            // !!!! Should we do configureMsgMoreStyle for SLL as well? And
            // same question in sendRawBuffer
            configureMsgMoreStyle(fd, false /*msg_more_style*/);
#endif

#ifdef __APPLE__
            off_t len_as_off_t = (off_t)len;

            // NB: The order of the first two parameters for ::sendfile are the
            // opposite way round in macOS vs. Linux
            // Also, in macOS sendfile returns zero for success, whereas in
            // Linux upon success it returns number of bytes written
            //
            // Although macOS sendfile man page is silent on the matter, by
            // experimentation it appears sendfile does not advance the file
            // position of "file", which is the same as the behavior described
            // in Linux sendfile man page
            int sendfile_res = ::sendfile(file, GET_ACTUAL_FD(fd),
                                          offset, &len_as_off_t,
                                          NULL, // no new prefix/suffix content
                                          0 /*reserved, must be zero*/);

            if (sendfile_res == 0)
            {
                bytesWritten = (ssize_t)len_as_off_t;
                offset += len_as_off_t; // to match what Linux sendfile does
            }
            else
            {
                bytesWritten = -1;
            }

#else
        bytesWritten = ::sendfile(GET_ACTUAL_FD(fd), file, &offset, len);
#endif

            PS_LOG_DEBUG_ARGS("::sendfile fd %" PIST_QUOTE(PS_FD_PRNTFCD) " , bytesWritten %d", fd, bytesWritten);

#ifdef PISTACHE_USE_SSL
        }
#endif /* PISTACHE_USE_SSL */

        return bytesWritten;
    }

    void Transport::armTimerMs(Fd fd, std::chrono::milliseconds value,
                               Async::Deferred<uint64_t> deferred)
    {
        PS_TIMEDBG_START_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD), fd);

        auto ctx                   = context();
        const bool isInRightThread = std::this_thread::get_id() == ctx.thread();
        TimerEntry entry(fd, value, std::move(deferred));

        if (!isInRightThread)
        {
            PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", timersQueue.push",
                              fd);
            timersQueue.push(std::move(entry));
        }
        else
        {
            PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", armTimerMsImpl",
                              fd);
            armTimerMsImpl(std::move(entry));
        }
    }

    void Transport::armTimerMsImpl(TimerEntry entry)
    {
#ifdef DEBUG
        Fd entry_fd = entry.fd;

#ifdef _USE_LIBEVENT
        PS_LOG_DEBUG_ARGS("entry.fd %" PIST_QUOTE(PS_FD_PRNTFCD) ", "
                                                                 "fd->type %d",
                          entry_fd,
                          EventMethFns::getEmEventType(entry_fd));
#else
        PS_LOG_DEBUG_ARGS("entry.fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                          entry_fd);
#endif

#endif

        auto it = timers.find(entry.fd);
        if (it != std::end(timers))
        {
            PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD),
                              "timer already armed",
                              entry.fd);

            entry.deferred.reject(std::runtime_error("Timer is already armed"));
            return;
        }

        int res = -1;
#ifdef _USE_LIBEVENT
        assert(entry.fd != PS_FD_EMPTY);
        res = EventMethFns::setEmEventTime(entry.fd, &(entry.value));
#else
        itimerspec spec;
        spec.it_interval.tv_sec  = 0;
        spec.it_interval.tv_nsec = 0;

        if (entry.value.count() < 1000)
        {
            spec.it_value.tv_sec  = 0;
            spec.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(entry.value)
                                        .count();
        }
        else
        {
            spec.it_value.tv_sec  = std::chrono::duration_cast<std::chrono::seconds>(entry.value).count();
            spec.it_value.tv_nsec = 0;
        }

        res = timerfd_settime(entry.fd, 0, &spec, nullptr);
#endif
        if (res == -1)
        {
            PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD) ",  ernno %d %s",
                              entry.fd, errno, strerror(errno));

            entry.deferred.reject(Pistache::Error::system("Could not set timer time"));
            return;
        }

        reactor()->registerFdOneShot(key(), entry.fd, NotifyOn::Read,
                                     Polling::Mode::Edge);

        PS_LOG_DEBUG_ARGS("Timer %" PIST_QUOTE(PS_FD_PRNTFCD) " inserting into timers", entry.fd);
        timers.insert(std::make_pair(entry.fd, std::move(entry)));
    }

    void Transport::handleWriteQueue(bool flush)
    {
        // Let's drain the queue
        for (;;)
        {
            auto write = writesQueue.popSafe();
            if (!write)
                break;

            auto fd = write->peerFd;
            if (!isPeerFd(fd))
                continue;

            {
                Guard guard(toWriteLock);
                toWrite[fd].push_back(std::move(*write));
            }

            reactor()->modifyFd(key(), fd, NotifyOn::Read | NotifyOn::Write,
                                Polling::Mode::Edge);

            if (flush)
                asyncWriteImpl(fd);
        }
    }

    void Transport::handleTimerQueue()
    {
        PS_TIMEDBG_START_THIS;

        for (;;)
        {
            auto timer = timersQueue.popSafe();
            if (!timer)
                break;

            armTimerMsImpl(std::move(*timer));
        }
    }

    void Transport::handlePeerQueue()
    {
        PS_TIMEDBG_START_THIS;

        for (;;)
        {
            auto data = peersQueue.popSafe();
            PS_LOG_DEBUG_ARGS("data %p", data.get());
            if (!data)
                break;

            handlePeer(data->peer);
        }
    }

    void Transport::handlePeer(const std::shared_ptr<Peer>& peer)
    {
        PS_TIMEDBG_START_THIS;

        Fd fd = peer->fd();

        {
            // See comment in transport.h on why peers_ must be mutex-protected
            std::lock_guard<std::mutex> l_guard(peers_mutex_);

            auto auto_insert_res_pr = peers_.insert(std::make_pair(fd, peer));
            if (!auto_insert_res_pr.second)
                PS_LOG_WARNING_ARGS("Failed to insert peer %p", peer.get());
        }

        peer->associateTransport(this);

        handler_->onConnection(peer);
        reactor()->registerFd(key(), fd, NotifyOn::Read | NotifyOn::Shutdown,
                              Polling::Mode::Edge);
    }

    void Transport::handleNotify()
    {
        PS_TIMEDBG_START_THIS;

        while (this->notifier.tryRead())
            ;

        rusage now;

        auto res = getrusage(
#ifdef _USE_LIBEVENT_LIKE_APPLE
            RUSAGE_SELF, // usage for whole process, not just current thread
                         // (macOS getrusage doesn't support RUSAGE_THREAD)
#else
            RUSAGE_THREAD,
#endif
            &now);
        if (res == -1)
            loadRequest_.reject(std::runtime_error("Could not compute usage"));

        loadRequest_.resolve(now);
        loadRequest_.clear();
    }

    void Transport::handleTimer(TimerEntry entry)
    {
        PS_TIMEDBG_START_THIS;

        if (entry.isActive())
        {
            uint64_t numWakeups;

            auto res = READ_FD(entry.fd, &numWakeups, sizeof numWakeups);
            if (res == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                else
                    entry.deferred.reject(
                        Pistache::Error::system("Could not read timerfd"));
            }
            else
            {
                if (res != sizeof(numWakeups))
                {
                    entry.deferred.reject(
                        Pistache::Error("Read invalid number of bytes for timer fd: " + std::to_string(GET_ACTUAL_FD(entry.fd))));
                }
                else
                {
                    entry.deferred.resolve(numWakeups);
                }
            }
        }
    }

    bool Transport::isPeerFd(FdConst fdconst) const
    {
        PS_TIMEDBG_START_THIS;

        // Cast away const so we can lock the mutex. Nonetheless, this function
        // overall locks then unlocks the mutex, leaving it unchanged
        Transport* non_const_this = (Transport*)this;

        std::lock_guard<std::mutex> l_guard(non_const_this->peers_mutex_);
        return (isPeerFdNoPeersMutexLock(fdconst));
    }

    // isPeerFdNoPeersMutexLock only when peers_mutex_ has been already locked
    bool Transport::isPeerFdNoPeersMutexLock(FdConst fdconst) const
    {
        PS_TIMEDBG_START_THIS;

        // Can cast away const since we're not actually going to change fd
        Fd fd = ((Fd)fdconst);

        return peers_.find(fd) != std::end(peers_);
    }

    bool Transport::isTimerFd(FdConst fdconst) const
    {
        PS_TIMEDBG_START_THIS;

        // Can cast away const since we're not actually going to change fd
        Fd fd    = ((Fd)fdconst);
        bool res = (timers.find(fd) != std::end(timers));

        PS_LOG_DEBUG_ARGS("Fd %" PIST_QUOTE(PS_FD_PRNTFCD) " %s in timers",
                          fdconst, (res ? "is" : "is not"));
        return res;
    }

    bool Transport::isPeerFd(Polling::Tag tag) const
    {
        PS_TIMEDBG_START_THIS;

        return isPeerFd(static_cast<FdConst>(tag.value()));
    }
    bool Transport::isTimerFd(Polling::Tag tag) const
    {
        return isTimerFd(static_cast<FdConst>(tag.value()));
    }

    std::shared_ptr<Peer>& Transport::getPeer(FdConst fdconst)
    {
        PS_TIMEDBG_START_THIS;

        // Can cast away const since we're not actually going to change fd
        Fd fd = ((Fd)fdconst);

        // See comment in transport.h on why peers_ must be mutex-protected
        std::lock_guard<std::mutex> l_guard(peers_mutex_);

        auto it = peers_.find(fd);
        if (it == std::end(peers_))
        {
            throw std::runtime_error("No peer found for fd: " + std::to_string(GET_ACTUAL_FD(fd)));
        }
        return it->second;
    }

    std::shared_ptr<Peer>& Transport::getPeer(Polling::Tag tag)
    {
        return getPeer(static_cast<FdConst>(tag.value()));
    }

    std::deque<std::shared_ptr<Peer>> Transport::getAllPeer()
    {
        std::deque<std::shared_ptr<Peer>> dqPeers;

        {
            // See comment in transport.h on why peers_ must be mutex-protected
            std::lock_guard<std::mutex> l_guard(peers_mutex_);

            for (const auto& peerPair : peers_)
            {
                if (isPeerFdNoPeersMutexLock(peerPair.first))
                {
                    dqPeers.push_back(peerPair.second);
                }
            }
        }

        return dqPeers;
    }

} // namespace Pistache::Tcp
