/* endpoint.cc
   Mathieu Stefani, 22 janvier 2016

   Implementation of the http endpoint
*/

#include <pistache/config.h>
#include <pistache/endpoint.h>
#include <pistache/peer.h>
#include <pistache/tcp.h>

#include <array>
#include <chrono>

namespace Pistache
{
    namespace Http
    {

        class TransportImpl : public Tcp::Transport
        {
        public:
            using Base = Tcp::Transport;

            explicit TransportImpl(const std::shared_ptr<Tcp::Handler>& handler);

            void registerPoller(Polling::Epoll& poller) override;
            void onReady(const Aio::FdSet& fds) override;

            void setHeaderTimeout(std::chrono::milliseconds timeout);
            void setBodyTimeout(std::chrono::milliseconds timeout);

            std::shared_ptr<Aio::Handler> clone() const override;

        private:
            std::shared_ptr<Tcp::Handler> handler_;
            std::chrono::milliseconds headerTimeout_;
            std::chrono::milliseconds bodyTimeout_;

            int timerFd;

            void checkIdlePeers();
        };

        TransportImpl::TransportImpl(const std::shared_ptr<Tcp::Handler>& handler)
            : Tcp::Transport(handler)
            , handler_(handler)
        { }

        void TransportImpl::registerPoller(Polling::Epoll& poller)
        {
            Base::registerPoller(poller);

            timerFd = TRY_RET(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

            static constexpr auto TimerInterval   = std::chrono::milliseconds(500);
            static constexpr auto TimerIntervalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(TimerInterval);

            static_assert(
                TimerInterval < std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)),
                "Timer frequency should be less than 1 second");

            itimerspec spec;
            spec.it_value.tv_sec  = 0;
            spec.it_value.tv_nsec = TimerIntervalNs.count();

            spec.it_interval.tv_sec  = 0;
            spec.it_interval.tv_nsec = TimerIntervalNs.count();

            TRY(timerfd_settime(timerFd, 0, &spec, 0));

            Polling::Tag tag(timerFd);
            poller.addFd(timerFd, Flags<Polling::NotifyOn>(Polling::NotifyOn::Read), Polling::Tag(timerFd));
        }

        void TransportImpl::onReady(const Aio::FdSet& fds)
        {
            bool handled = false;
            for (const auto& entry : fds)
            {
                if (entry.getTag() == Polling::Tag(timerFd))
                {
                    uint64_t wakeups;
                    ::read(timerFd, &wakeups, sizeof wakeups);
                    checkIdlePeers();
                    handled = true;
                }
            }

            if (!handled)
                Base::onReady(fds);
        }

        void TransportImpl::setHeaderTimeout(std::chrono::milliseconds timeout)
        {
            headerTimeout_ = timeout;
        }
        void TransportImpl::setBodyTimeout(std::chrono::milliseconds timeout)
        {
            bodyTimeout_ = timeout;
        }

        void TransportImpl::checkIdlePeers()
        {
            std::vector<std::shared_ptr<Tcp::Peer>> idlePeers;

            for (const auto& peerPair : peers)
            {
                const auto& peer = peerPair.second;
                auto parser      = Http::Handler::getParser(peer);
                auto time        = parser->time();

                auto now     = std::chrono::steady_clock::now();
                auto elapsed = now - time;

                auto* step = parser->step();
                if (step->id() == Private::RequestLineStep::Id)
                {
                    if (elapsed > headerTimeout_ || elapsed > bodyTimeout_)
                        idlePeers.push_back(peer);
                }
                else if (step->id() == Private::HeadersStep::Id)
                {
                    if (elapsed > bodyTimeout_)
                        idlePeers.push_back(peer);
                }
            }

            for (const auto& idlePeer : idlePeers)
            {
                ResponseWriter response(Http::Version::Http11, this, static_cast<Http::Handler*>(handler_.get()), idlePeer);
                response.send(Http::Code::Request_Timeout).then([=](ssize_t) { removePeer(idlePeer); }, [=](std::exception_ptr) { removePeer(idlePeer); });
            }
        }

        std::shared_ptr<Aio::Handler> TransportImpl::clone() const
        {
            auto transport = std::make_shared<TransportImpl>(handler_->clone());
            transport->setHeaderTimeout(headerTimeout_);
            transport->setBodyTimeout(bodyTimeout_);
            return transport;
        }

        Endpoint::Options::Options()
            : threads_(1)
            , flags_()
            , backlog_(Const::MaxBacklog)
            , maxRequestSize_(Const::DefaultMaxRequestSize)
            , maxResponseSize_(Const::DefaultMaxResponseSize)
            , headerTimeout_(Const::DefaultHeaderTimeout)
            , bodyTimeout_(Const::DefaultBodyTimeout)
            , logger_(PISTACHE_NULL_STRING_LOGGER)
        { }

        Endpoint::Options& Endpoint::Options::threads(int val)
        {
            threads_ = val;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::threadsName(const std::string& val)
        {
            threadsName_ = val;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::flags(Flags<Tcp::Options> flags)
        {
            flags_ = flags;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::backlog(int val)
        {
            backlog_ = val;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::maxRequestSize(size_t val)
        {
            maxRequestSize_ = val;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::maxPayload(size_t val)
        {
            return maxRequestSize(val);
        }

        Endpoint::Options& Endpoint::Options::maxResponseSize(size_t val)
        {
            maxResponseSize_ = val;
            return *this;
        }

        Endpoint::Options& Endpoint::Options::logger(PISTACHE_STRING_LOGGER_T logger)
        {
            logger_ = logger;
            return *this;
        }

        Endpoint::Endpoint() { }

        Endpoint::Endpoint(const Address& addr)
            : listener(addr)
        { }

        void Endpoint::init(const Endpoint::Options& options)
        {
            listener.init(options.threads_, options.flags_, options.threadsName_);
            listener.setTransportFactory([&] {
                if (!handler_)
                    throw std::runtime_error("Must call setHandler()");

                auto transport = std::make_shared<TransportImpl>(handler_);
                transport->setHeaderTimeout(options.headerTimeout_);
                transport->setBodyTimeout(options.bodyTimeout_);

                return transport;
            });

            options_ = options;
            logger_  = options.logger_;
        }

        void Endpoint::setHandler(const std::shared_ptr<Handler>& handler)
        {
            handler_ = handler;
            handler_->setMaxRequestSize(options_.maxRequestSize_);
            handler_->setMaxResponseSize(options_.maxResponseSize_);
        }

        void Endpoint::bind() { listener.bind(); }

        void Endpoint::bind(const Address& addr) { listener.bind(addr); }

        void Endpoint::serve() { serveImpl(&Tcp::Listener::run); }

        void Endpoint::serveThreaded() { serveImpl(&Tcp::Listener::runThreaded); }

        void Endpoint::shutdown() { listener.shutdown(); }

        void Endpoint::useSSL(const std::string& cert, const std::string& key, bool use_compression)
        {
#ifndef PISTACHE_USE_SSL
            (void)cert;
            (void)key;
            (void)use_compression;
            throw std::runtime_error("Pistache is not compiled with SSL support.");
#else
            listener.setupSSL(cert, key, use_compression);
#endif /* PISTACHE_USE_SSL */
        }

        void Endpoint::useSSLAuth(std::string ca_file, std::string ca_path,
                                  int (*cb)(int, void*))
        {
#ifndef PISTACHE_USE_SSL
            (void)ca_file;
            (void)ca_path;
            (void)cb;
            throw std::runtime_error("Pistache is not compiled with SSL support.");
#else
            listener.setupSSLAuth(ca_file, ca_path, cb);
#endif /* PISTACHE_USE_SSL */
        }

        Async::Promise<Tcp::Listener::Load>
        Endpoint::requestLoad(const Tcp::Listener::Load& old)
        {
            return listener.requestLoad(old);
        }

        Endpoint::Options Endpoint::options() { return Options(); }

    } // namespace Http
} // namespace Pistache
