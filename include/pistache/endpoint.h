/*
   Mathieu Stefani, 22 janvier 2016

   An Http endpoint
*/

#pragma once

#include <pistache/listener.h>
#include <pistache/net.h>
#include <pistache/http.h>

namespace Pistache {
namespace Http {

class Endpoint {
public:
    struct Options {
        friend class Endpoint;

        Options& threads(int val);
        Options& flags(Flags<Tcp::Options> flags);
        Options& backlog(int val);
        Options& maxPayload(size_t val);

    private:
        int threads_;
        Flags<Tcp::Options> flags_;
        int backlog_;
        size_t maxPayload_;
        Options();
    };
    Endpoint();
    explicit Endpoint(const Address& addr);

    template<typename... Args>
    void initArgs(Args&& ...args) {
        listener.init(std::forward<Args>(args)...);
    }

    void init(const Options& options = Options());
    void setHandler(const std::shared_ptr<Handler>& handler);

    void bind();
    void bind(const Address& addr);

    void serve();
    void serveThreaded();

    void shutdown();

    /*!
     * \brief Use SSL on this endpoint
     *
     * \param[in] cert Server certificate path
     * \param[in] key Server key path
     * \param[in] use_compression Wether or not use compression on the encryption
     *
     * Setup the SSL configuration for an endpoint. In order to do that, this
     * function will init OpenSSL constants and load *all* algorithms. It will
     * then load the server certificate and key, in order to use it later.
     * *If the private key does not match the certificate, an exception will
     * be thrown*
     *
     * \note use_compression is false by default to mitigate BREACH[1] and
     *          CRIME[2] vulnerabilities
     * \note This function will throw an exception if pistache has not been
     *          compiled with PISTACHE_USE_SSL
     *
     * [1] https://en.wikipedia.org/wiki/BREACH
     * [2] https://en.wikipedia.org/wiki/CRIME
     */
    void useSSL(std::string cert, std::string key, bool use_compression = false);

    /*!
     * \brief Use SSL certificate authentication on this endpoint
     *
     * \param[in] ca_file Certificate Authority file
     * \param[in] ca_path Certificate Authority path
     * \param[in] cb OpenSSL verify callback[1]
     *
     * Change the SSL configuration in order to only accept verified client
     * certificates. The function 'useSSL' *should* be called before this
     * function.
     * Due to the way we actually doesn't expose any OpenSSL internal types, the
     * callback function is Cpp generic. The 'real' callback will be:
     *
     *     int callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
     *
     * It is up to the caller to cast the second argument to an appropriate
     * pointer:
     *
     *     int store_callback(int preverify_ok, void *ctx) {
     *         X509_STORE_CTX *x509_ctx = (X509_STORE_CTX *)ctx;
     *
     *         [...]
     *
     *         if (all_good)
     *              return 1;
     *         return 0;
     *     }
     *
     *     [...]
     *
     *     endpoint->useSSLAuth(ca_file, ca_path, &store_callback);
     *
     * See the documentation[1] for more information about this callback.
     *
     * \sa useSSL
     * \note This function will throw an exception if pistache has not been
     *          compiled with PISTACHE_USE_SSL
     *
     * [1] https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_verify.html
     */
    void useSSLAuth(std::string ca_file, std::string ca_path = "",  int (*cb)(int, void *) = NULL);

    bool isBound() const {
        return listener.isBound();
    }

    Port getPort() const {
        return listener.getPort();
    }

    Async::Promise<Tcp::Listener::Load> requestLoad(const Tcp::Listener::Load& old);

    static Options options();

private:

    template<typename Method>
    void serveImpl(Method method)
    {
#define CALL_MEMBER_FN(obj, pmf)  ((obj).*(pmf))
        if (!handler_)
            throw std::runtime_error("Must call setHandler() prior to serve()");

        listener.setHandler(handler_);
        listener.bind();

        CALL_MEMBER_FN(listener, method)();
#undef CALL_MEMBER_FN
    }

    std::shared_ptr<Handler> handler_;
    Tcp::Listener listener;
};

template<typename Handler>
void listenAndServe(Address addr, const Endpoint::Options& options = Endpoint::options())
{
    Endpoint endpoint(addr);
    endpoint.init(options);
    endpoint.setHandler(make_handler<Handler>());
    endpoint.serve();
}


} // namespace Http
} // namespace Pistache
