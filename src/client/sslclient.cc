/******************************************************************************
 * sslclient.h
 *
 * C++ callable low-level SSL client capabilities.
 *
 */

#include "pistache/ssl_do_or_dont.h"
#ifdef PIST_INCLUDE_SSL

/*****************************************************************************/

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include "pistache/ssl_async.h"

#include "pistache/sslclient.h"

namespace Pistache {

namespace Http {

/*****************************************************************************/
    

class SslConnectionImpl
{
private:
    const std::string mHostName;
    unsigned int mHostPort;
    const std::string mHostResource;

    SslAsyncSPtr mSslAsync;

public:
    SslConnectionImpl(const std::string & _hostName, unsigned int _hostPort,
                      const std::string & _hostResource,
                      const std::string * _hostChainPemFile);

    ssize_t sslRawRecv(void * _buffer, size_t _length);
    ssize_t sslRawSend(const void * _buffer, size_t _length);
    
    int getFd() const; // rets FD of underlying socket, or < 0 if not open
    int close(); // as per posix close

    ~SslConnectionImpl();
};

// ---------------------------------------------------------------------------

SslConnectionImpl::SslConnectionImpl(const std::string & _hostName,
                                     unsigned int _hostPort,
                                     const std::string & _hostResource,
                                     const std::string * _hostChainPemFile) :
    mHostName(_hostName), mHostPort(_hostPort), mHostResource(_hostResource)
{
    if (mHostPort == 0)
        mHostPort = 443;

    SslAsyncSPtr cli = std::make_shared<SslAsync>(mHostName.c_str(), mHostPort,
                        _hostChainPemFile ? _hostChainPemFile->c_str() : NULL);

    if (!cli)
        throw(std::runtime_error("Null SslAsync on open"));

    int fd = cli->getFd();
    if (fd <= 0)
    {
        cli = NULL;
        throw(std::runtime_error("Bad fd on open"));
    }

    mSslAsync = cli;
}

// ---------------------------------------------------------------------------

SslConnectionImpl::~SslConnectionImpl()
{
    mSslAsync = NULL;
}

// ---------------------------------------------------------------------------

ssize_t SslConnectionImpl::sslRawRecv(void * _buffer, size_t _length)
{
    if (!mSslAsync)
    {
        errno = EBADF;
        return(-1);
    }

    if (!_buffer)
    {
        errno = EINVAL;
        return(-1);
    }

    ssize_t res_len = mSslAsync->sslAppRecv(_buffer, _length);
    return(res_len);
}
    
// ---------------------------------------------------------------------------

ssize_t SslConnectionImpl::sslRawSend(const void * _buffer, size_t _length)
{
    if (!mSslAsync)
    {
        errno = EBADF;
        return(-1);
    }

    if (!_buffer)
    {
        errno = EINVAL;
        return(-1);
    }

    ssize_t res_len = mSslAsync->sslAppSend(_buffer, _length);
    return(res_len);
}
    
// ---------------------------------------------------------------------------

// rets FD of underlying socket, or < 0 if not open
int SslConnectionImpl::getFd() const
{
    if (!mSslAsync)
        return(-1);

    return(mSslAsync->getFd());
}

// ---------------------------------------------------------------------------

int SslConnectionImpl::close() // as per posix close
{
    if (!mSslAsync)
    {
        errno = EBADF;
        return(-1);
    }

    mSslAsync = NULL;

    return(0);
}

/*****************************************************************************/

SslConnection::SslConnection(const std::string & _hostName,
                             unsigned int _hostPort,
                             const std::string & _hostResource,
                             const std::string * _hostChainPemFile)
{
    mImpl = std::make_shared<SslConnectionImpl>(_hostName, _hostPort,
                                             _hostResource, _hostChainPemFile);
    if (!mImpl)
        throw(std::runtime_error("Failed to alloc SslConnectionImpl"));
}

// ---------------------------------------------------------------------------

ssize_t SslConnection::sslRawRecv(void * _buffer, size_t _length)
{
    return(mImpl->sslRawRecv(_buffer, _length));
}

ssize_t SslConnection::sslRawSend(const void * _buffer, size_t _length)
{
    return(mImpl->sslRawSend(_buffer, _length));
}
    
// ---------------------------------------------------------------------------

// rets FD of underlying socket, or < 0 if not open
int SslConnection::getFd() const
{
    return(mImpl->getFd());
}

// ---------------------------------------------------------------------------

int SslConnection::close() // as per posix close
{
    return(mImpl->close());
}
    
/*****************************************************************************/

} // namespace Http

} // namespace Net
    
/*****************************************************************************/

#endif // of ifdef PIST_INCLUDE_SSL

/*****************************************************************************/

