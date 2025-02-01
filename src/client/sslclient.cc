/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani, 2017, 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * sslclient.h
 *
 * C++ callable low-level SSL client capabilities.
 *
 */

#include <string>
#include <stdexcept> // runtime

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <pistache/ssl_async.h>
// #include "ssl_client_nonblock.h"
// #include "openssl-bio.h"

#include <pistache/sslclient.h>

namespace Pistache::Http::Experimental
{

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
                      int _domain, // AF_INET or AF_INET6
                      const std::string & _hostResource,
                      bool _doVerification,
                      const std::string * _hostChainPemFile);

    // Note: read(...) removed since not used

    PST_SSIZE_T sslRawRecv(void * _buffer, size_t _length);
    PST_SSIZE_T sslRawSend(const void * _buffer, size_t _length);

    Fd getFd() const; // rets FD of underlying socket, or < 0 if not open
    int close(); // as per posix close

    ~SslConnectionImpl();
};

// ---------------------------------------------------------------------------

SslConnectionImpl::SslConnectionImpl(const std::string & _hostName,
                                     unsigned int _hostPort,
                                     int _domain, // AF_INET or AF_INET6
                                     const std::string & _hostResource,
                                     bool _doVerification,
                                     const std::string * _hostChainPemFile) :
    mHostName(_hostName), mHostPort(_hostPort), mHostResource(_hostResource)
{
    if (mHostPort == 0)
        mHostPort = 443;

    SslAsyncSPtr cli = std::make_shared<SslAsync>(mHostName.c_str(), mHostPort,
                        _domain,
                        _doVerification,
                        _hostChainPemFile ? _hostChainPemFile->c_str() : NULL);

    if (!cli)
        throw(std::runtime_error("Null SslAsync on open"));

    Fd fd = cli->getFd();
    if (fd == PS_FD_EMPTY)
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

// Note: read(...) removed since not used

// ---------------------------------------------------------------------------

PST_SSIZE_T SslConnectionImpl::sslRawRecv(void * _buffer, size_t _length)
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

    PST_SSIZE_T res_len = mSslAsync->sslAppRecv(_buffer, _length);
    return(res_len);
}

// ---------------------------------------------------------------------------

PST_SSIZE_T SslConnectionImpl::sslRawSend(const void * _buffer, size_t _length)
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

    PST_SSIZE_T res_len = mSslAsync->sslAppSend(_buffer, _length);
    return(res_len);
}

// ---------------------------------------------------------------------------

// rets FD of underlying socket, or PS_FD_EMPTY if not open
Fd SslConnectionImpl::getFd() const
{
    if (!mSslAsync)
        return(PS_FD_EMPTY);

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
                             int _domain, // AF_INET or AF_INET6
                             const std::string & _hostResource,
                             bool _doVerification,
                             const std::string * _hostChainPemFile)
{
    mImpl = std::make_shared<SslConnectionImpl>(_hostName, _hostPort,
                                                _domain,
                                                _hostResource,
                                                _doVerification,
                                                _hostChainPemFile);
    if (!mImpl)
        throw(std::runtime_error("Failed to alloc SslConnectionImpl"));
}

// ---------------------------------------------------------------------------

// Note: read(...) removed since not used

PST_SSIZE_T SslConnection::sslRawRecv(void * _buffer, size_t _length)
{
    return(mImpl->sslRawRecv(_buffer, _length));
}

PST_SSIZE_T SslConnection::sslRawSend(const void * _buffer, size_t _length)
{
    return(mImpl->sslRawSend(_buffer, _length));
}

// ---------------------------------------------------------------------------

// rets FD of underlying socket, or < 0 if not open
Fd SslConnection::getFd() const
{
    return(mImpl->getFd());
}

// ---------------------------------------------------------------------------

int SslConnection::close() // as per posix close
{
    return(mImpl->close());
}

/*****************************************************************************/

} // namespace Pistache::Http::Experimental
