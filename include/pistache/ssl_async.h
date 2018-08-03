/******************************************************************************
 * ssl_async.h
 *
 * Used by sslclient.h/.cc
 *
 *
 */

#ifndef INCLUDED_SSL_ASYNC_H
#define INCLUDED_SSL_ASYNC_H

#include "pistache/ssl_do_or_dont.h"
#ifdef PIST_INCLUDE_SSL

#include <memory>
#include <vector>
#include <mutex>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

// ---------------------------------------------------------------------------

class SslAsync
{
private:
    // Mutex is used to protect all the classes (private) state variables as
    // well as operations on the socket (mFd)
    // It is claimed on entry to the pubicly accesible member functions
    std::mutex mSslAsyncMutex;
    
private:
    int mFd;
    
    std::vector<uint8_t> mToWriteVec;
    std::vector<uint8_t> mReadFromVec;

private:
    int mWantsTcpRead;
    int mWantsTcpWrite;
    int mCallSslReadInsteadOfWrite;
    int mCallSslWriteInsteadOfRead;
    int mConnecting;

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
    
    void checkSocket(bool _forAppRead);

private:
    ACTION ssl_read();
    ACTION ssl_write();

public:
    ssize_t sslAppRecv(void * _buffer, size_t _length);
    ssize_t sslAppSend(const void * _buffer, size_t _length);

public:
    SslAsync(const char * _hostName, unsigned int _hostPort,
             const char * _hostChainPemFile);
    ~SslAsync();

    int getFd() const { return(mFd); }
};
typedef std::shared_ptr<SslAsync> SslAsyncSPtr;
typedef std::shared_ptr<const SslAsync> SslAsyncSPtrC;


// ---------------------------------------------------------------------------

#endif // of ifdef PIST_INCLUDE_SSL

#endif // of ifndef INCLUDED_SSL_ASYNC_H

// ---------------------------------------------------------------------------
