/*
 * SPDX-FileCopyrightText: 2017, 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * ssl_async.h
 *
 * See ssl_async.cc for additional references and license acknowledgements.
 *
 */

#ifndef INCLUDED_SSL_ASYNC_H
#define INCLUDED_SSL_ASYNC_H

#include <memory>
#include <vector>
#include <mutex>

#include <pistache/eventmeth.h> // for Fd

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

// ---------------------------------------------------------------------------

namespace Pistache::Http::Experimental {

// ---------------------------------------------------------------------------

class SslAsync
{
private:
    // Mutex is used to protect all the classes (private) state variables as
    // well as operations on the socket (mFd)
    // It is claimed on entry to the pubicly accesible member functions
    std::mutex mSslAsyncMutex;

private:
    Pistache::Fd mFd;

    std::vector<uint8_t> mToWriteVec;
    std::vector<uint8_t> mReadFromVec;

private:
    int mWantsTcpRead;
    int mWantsTcpWrite;
    int mCallSslReadForSslLib;
    int mCallSslWriteForSslLib;
    int mConnecting;
    bool mDoVerification;

private:
    SSL * mSsl;
    SSL_CTX * mCtxt;

private:
    typedef enum {
        CONTINUE,
        BREAK,
        NEITHER
    } ACTION;
    ACTION ssl_connect();

    int checkSocket(bool _forAppRead);

private:
    ACTION ssl_read();
    ACTION ssl_write();

public:
    PST_SSIZE_T sslAppRecv(void * _buffer, size_t _length);
    PST_SSIZE_T sslAppSend(const void * _buffer, size_t _length);

public:
    SslAsync(const char * _hostName, unsigned int _hostPort,
             int _domain, // AF_INET or AF_INET6
             bool _doVerification,
             const char * _hostChainPemFile);
    ~SslAsync();

    Fd getFd() const { return(mFd); }
};
typedef std::shared_ptr<SslAsync> SslAsyncSPtr;
typedef std::shared_ptr<const SslAsync> SslAsyncSPtrC;


// ---------------------------------------------------------------------------

} // namespace Pistache::Http::Experimental

#endif // of ifndef INCLUDED_SSL_ASYNC_H

// ---------------------------------------------------------------------------
