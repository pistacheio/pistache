#pragma once

#ifdef PISTACHE_USE_SSL
#include <openssl/ssl.h>
#endif

#include <memory>

namespace Pistache {
namespace ssl {

struct SSLCtxDeleter {
  void operator()(void *ptr) {
#ifdef PISTACHE_USE_SSL
    SSL_CTX_free(reinterpret_cast<SSL_CTX *>(ptr));

    // EVP_cleanup call is not related to cleaning SSL_CTX, just global cleanup routine.
    // TODO: Think about removing EVP_cleanup call at all
    // It was deprecated in openssl 1.1.0 version (see
    // https://www.openssl.org/news/changelog.txt):
    // "Make various cleanup routines no-ops and mark them as deprecated."
    EVP_cleanup();
#else
    (void)ptr;
#endif
  }
};

using SSLCtxPtr = std::unique_ptr<void, SSLCtxDeleter>;

#ifdef PISTACHE_USE_SSL
inline SSL_CTX* GetSSLContext(ssl::SSLCtxPtr &ctx) {
    return reinterpret_cast<SSL_CTX *>(ctx.get());
}
#endif

}
}
