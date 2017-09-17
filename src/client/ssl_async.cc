/******************************************************************************
 * ssl_async.cc
 *
 * This code is adapted in significant part from: 
 * https://codereview.stackexchange.com/questions/108600/
 *                                               complete-async-openssl-example
 *
 * Code on stackexchange is licensed under the MIT license, except that the
 * notice provisions of the MIT license are waived absent a request of the
 * code contributor or of StackExchange. In this case, we do not see any such
 * request, but the terms of the MIT license are nonetheless hereby
 * acknowledged.
 * Non-code contributions (explanations etc.) on stackexchange are permissively
 * licensed under the Creative Commons license (CC BY-SA 3.0)
 *
 * A futher portion of the code is derived from model implementations provided
 * by openssl.org in Openssl-bio-fetch.tar.gz at
 * https://wiki.openssl.org/index.php/SSL/TLS_Client.
 * License: openssl permits modifications redistributions and use, provided
 * certain notices are retained and acknowledgements made
 * (https://www.openssl.org/source/license.html)
 *
 */

#include "pistache/ssl_do_or_dont.h"
#ifdef PIST_INCLUDE_SSL

// ---------------------------------------------------------------------------

#include <mutex>

#include <stdio.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/conf.h> // for OPENSSL_config

// For GENERAL_NAMES stuff
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> // getaddrinfo
#include <netinet/tcp.h>
#include <sys/select.h>
#include <netinet/in.h>

#include <unistd.h> // for close

#include "pistache/ssl_async.h"

// !!!!!! Deal with third fdset in select

// ---------------------------------------------------------------------------

#define SSL_THROW(__MSG)                        \
        throw std::runtime_error(__MSG)

#define SSL_CLOSE_AND_THROW(__MSG)            \
    {                                                 \
        int current_fd = -1;                          \
        mFd = -1;                                     \
        close(current_fd);                            \
        throw std::runtime_error(__MSG);              \
    }
     

// ---------------------------------------------------------------------------

typedef enum {
  CONTINUE,
  BREAK,
  NEITHER
} ACTION;

// ---------------------------------------------------------------------------

ssize_t SslAsync::sslAppSend(const void * _buffer, size_t _length)
{
    if (!_length)
        return(0);

    if (!_buffer)
    {
        errno = EINVAL;
        return(-1);
    }

    std::lock_guard<std::mutex> grd(mSslAsyncMutex);

    std::size_t prior_size = mToWriteVec.size();
    std::size_t total_written = 0;

    mToWriteVec.insert(mToWriteVec.end(), (const uint8_t *)_buffer,
                       ((const uint8_t *)_buffer) + _length);

    std::size_t starting_size = 0;
    unsigned int loop_count = 0;
    for(; loop_count < 256; loop_count++)
    {
        starting_size = mToWriteVec.size();
        checkSocket(false/*not forAppRead*/);

        if (!mToWriteVec.size())
            return(_length);
    
        if (mToWriteVec.size() >= starting_size)
            break;

        total_written += (starting_size - mToWriteVec.size());
    }
    if ((loop_count >= 256) || (total_written <= prior_size))
    { // No write happened this time. Let go of this data, return error
        mToWriteVec.resize(prior_size - total_written);
        errno = EWOULDBLOCK;
        return(-1); // !!!!
    }

    return(total_written - prior_size);
}

// ---------------------------------------------------------------------------

SslAsync::ACTION SslAsync::ssl_connect()
{
  int result = SSL_connect(mSsl);
  if (result == 0) {
    long error = ERR_get_error();
    const char* error_str = ERR_error_string(error, NULL);
    return BREAK;
  } else if (result < 0) {
    int ssl_error = SSL_get_error(mSsl, result);
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
        mWantsTcpWrite = 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_WANT_READ) {
      // wants_tcp_read is always 1;
      return CONTINUE;
    }

    long error = ERR_get_error();
    const char* error_string = ERR_error_string(error, NULL);
    return BREAK;
  } else {
    mConnecting = 0;
    return CONTINUE;
  }

  return NEITHER;
}

// ---------------------------------------------------------------------------

ssize_t SslAsync::sslAppRecv(void * _buffer, size_t _length)
{
    if (!_buffer || !_length)
    {
        errno = EINVAL;
        return(-1);
    }

    std::lock_guard<std::mutex> grd(mSslAsyncMutex);
    
    errno = EWOULDBLOCK;
    
    checkSocket(true/*forAppRead*/);
    if (mReadFromVec.empty())
    {
        if (errno == ENODATA)
            return(0);
        if (errno == 0)
            errno = EWOULDBLOCK;
        return(-1);
    }

    ssize_t bytes_received = std::min(_length, mReadFromVec.size());

    memcpy(_buffer, mReadFromVec.data(), bytes_received);
    mReadFromVec.erase(mReadFromVec.begin(),
                       mReadFromVec.begin() + bytes_received);
    return(bytes_received);
}

// ---------------------------------------------------------------------------

SslAsync::ACTION SslAsync::ssl_read()
{
  mCallSslReadInsteadOfWrite = 0;

  char buffer[1536];
  int num = SSL_read(mSsl, buffer, sizeof(buffer));
  if (num == 0) {
    long error = ERR_get_error();
    const char* error_str = ERR_error_string(error, NULL);
    errno = ENODATA;
    return BREAK;
  } else if (num < 0) {
    int ssl_error = SSL_get_error(mSsl, num);
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
      mWantsTcpWrite = 1;
      mCallSslReadInsteadOfWrite = 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_WANT_READ) {
      // wants_tcp_read is always 1;
      return CONTINUE;
    }

    long error = ERR_get_error();
    const char* error_string = ERR_error_string(error, NULL);
    return BREAK;
  } else {
      mReadFromVec.insert(mReadFromVec.end(), &(buffer[0]), &(buffer[num]));
  }

  return NEITHER;
}

// ---------------------------------------------------------------------------


SslAsync::ACTION SslAsync::ssl_write()
{
  if (mCallSslWriteInsteadOfRead && (mToWriteVec.empty())) {
    return BREAK;
  }

  mCallSslReadInsteadOfWrite = 0;

  if (mToWriteVec.empty())
    return NEITHER;

  int num = SSL_write(mSsl, mToWriteVec.data(), mToWriteVec.size());
  if (num == 0) {
    long error = ERR_get_error();
    const char* error_str = ERR_error_string(error, NULL);
    return BREAK;
  } else if (num < 0) {
    int ssl_error = SSL_get_error(mSsl, num);
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
      mWantsTcpWrite = 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_WANT_READ) {
      mCallSslReadInsteadOfWrite = 1;
      // wants_tcp_read is always 1;
      return CONTINUE;
    }

    long error = ERR_get_error();
    const char* error_string = ERR_error_string(error, NULL);
    return BREAK;
  } else {
      if (mToWriteVec.size() < num)
      {
          mToWriteVec.clear();
          mWantsTcpWrite = 1;
      }
      else
      {
          mToWriteVec.erase(mToWriteVec.begin(), mToWriteVec.begin()+num);
          mWantsTcpWrite = 0;
      }
      
      
  }

  return NEITHER;
}

// ---------------------------------------------------------------------------

// Helper function to be called only by initOpenSslIfNotAlready
static void init_openssl_library(void)
{
    /* https://www.openssl.org/docs/ssl/SSL_library_init.html */
    (void)SSL_library_init();
    /* Cannot fail (always returns success) ??? */

    OpenSSL_add_all_algorithms();
    
    /* https://www.openssl.org/docs/crypto/ERR_load_crypto_strings.html */
    SSL_load_error_strings();
    /* Cannot fail ??? */

    // ERR_load_BIO_strings();
    ERR_load_crypto_strings();
    
    /* SSL_load_error_strings loads both libssl and libcrypto strings */
    /* ERR_load_crypto_strings(); */
    /* Cannot fail ??? */
    
    /* OpenSSL_config may or may not be called internally, based on */
    /*  some #defines and internal gyrations. Explicitly call it    */
    /*  *IF* you need something from openssl.cfg, such as a         */
    /*  dynamically configured ENGINE.                              */
    OPENSSL_config(NULL);
    /* Cannot fail ??? */
    
    /* Include <openssl/opensslconf.h> to get this define     */
#if defined (OPENSSL_THREADS)
    /* TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO */
    /* https://www.openssl.org/docs/crypto/threads.html */
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static bool lOpenSslInited = false;
static std::mutex lOpenSslInitedMutex;

static void initOpenSslIfNotAlready()
{
    if (lOpenSslInited)
        return;

    std::lock_guard<std::mutex> grd(lOpenSslInitedMutex);
    if (lOpenSslInited)
        return;
    
    init_openssl_library();
    lOpenSslInited = true;
}

// ---------------------------------------------------------------------------

static void logCnName(const char* label, X509_NAME* const name)
{
    int idx = -1, success = 0;
    unsigned char *utf8 = NULL;
    
    do
    {
        if(!name) break; /* failed */
        
        idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
        if(!(idx > -1))  break; /* failed */
        
        X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, idx);
        if(!entry) break; /* failed */
        
        ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
        if(!data) break; /* failed */
        
        int length = ASN1_STRING_to_UTF8(&utf8, data);
        if(!utf8 || !(length > 0))  break; /* failed */
        
        success = 1;
        
    } while (0);
    
    if(utf8)
        OPENSSL_free(utf8);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void logSanName(const char* label, X509* const cert)
{
    int success = 0;
    GENERAL_NAMES* names = NULL;
    unsigned char* utf8 = NULL;
    
    do
    {
        if(!cert) break; /* failed */
        
        names = (GENERAL_NAMES *)
                           X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0 );
        if(!names) break;
        
        int i = 0, count = sk_GENERAL_NAME_num(names);
        if(!count) break; /* failed */
        
        for( i = 0; i < count; ++i )
        {
            GENERAL_NAME* entry = sk_GENERAL_NAME_value(names, i);
            if(!entry) continue;
            
            if(GEN_DNS == entry->type)
            {
                int len1 = 0, len2 = -1;
                
                len1 = ASN1_STRING_to_UTF8(&utf8, entry->d.dNSName);
                if(utf8) {
                    len2 = (int)strlen((const char*)utf8);
                }
                
                /* If there's a problem with string lengths, then     */
                /* we skip the candidate and move on to the next.     */
                /* Another policy would be to fails since it probably */
                /* indicates the client is under attack.              */
                if(utf8 && len1 && len2 && (len1 == len2)) {
                    success = 1;
                }
                
                if(utf8) {
                    OPENSSL_free(utf8), utf8 = NULL;
                }
            }
        }

    } while (0);
    
    if(names)
        GENERAL_NAMES_free(names);
    
    if(utf8)
        OPENSSL_free(utf8);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int verify_callback(int preverify, X509_STORE_CTX* x509_ctx)
{
    /* For error codes, see http://www.openssl.org/docs/apps/verify.html  */
    
    int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
    int err = X509_STORE_CTX_get_error(x509_ctx);
    
    X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    X509_NAME* iname = cert ? X509_get_issuer_name(cert) : NULL;
    X509_NAME* sname = cert ? X509_get_subject_name(cert) : NULL;

    /* Issuer is the authority we trust that warrants nothing useful */
    logCnName("Issuer (cn)", iname);
    
    /* Subject is who the certificate is issued to by the authority  */
    logCnName("Subject (cn)", sname);
    
    if(depth == 0) {
        /* If depth is 0, its the server's certificate. Log the SANs */
        logSanName("Subject (san)", cert);
    }
    
    if(preverify == 0)
    {
        if(err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
            SSL_THROW(
                "Error = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY");
        else if(err == X509_V_ERR_CERT_UNTRUSTED)
            SSL_THROW("Error = X509_V_ERR_CERT_UNTRUSTED");
        else if(err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)
            SSL_THROW("Error = X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN");
        else if(err == X509_V_ERR_CERT_NOT_YET_VALID)
            SSL_THROW("Error = X509_V_ERR_CERT_NOT_YET_VALID");
        else if(err == X509_V_ERR_CERT_HAS_EXPIRED)
            SSL_THROW("Error = X509_V_ERR_CERT_HAS_EXPIRED");
        else if(err == X509_V_OK)
            SSL_THROW("Error = X509_V_OK");
        else
            SSL_THROW("SSL preverify error");
    }

#if !defined(NDEBUG)
    return 1;
#else
    return preverify;
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static SSL_CTX * makeSslCtx(const char * _hostChainPemFile)
{
    if ((!_hostChainPemFile) || (!strlen(_hostChainPemFile)))
    {
        errno = EINVAL;
        return(NULL);
    }

    SSL_CTX * ctx = NULL;

    initOpenSslIfNotAlready();

    do {
        /* https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
        const SSL_METHOD* method = SSLv23_method();
        unsigned long ssl_err = ERR_get_error();
        
        if(!(NULL != method))
        {
            break; /* failed */
        }
        
        /* http://www.openssl.org/docs/ssl/ctx_new.html */
        ctx = SSL_CTX_new(method);
        /* ctx = SSL_CTX_new(TLSv1_method()); */
        ssl_err = ERR_get_error();
        
        if(!(ctx != NULL))
        {
            break; /* failed */
        }
        
        /* https://www.openssl.org/docs/ssl/ctx_set_verify.html */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
        /* Cannot fail ??? */
        
        /* https://www.openssl.org/docs/ssl/ctx_set_verify.html */
        SSL_CTX_set_verify_depth(ctx, 5);
        /* Cannot fail ??? */

        /* Remove the most egregious. Because SSLv2 and SSLv3 have been      */
        /* removed, a TLSv1.0 handshake is used. The client accepts TLSv1.0  */
        /* and above. An added benefit of TLS 1.0 and above are TLS          */
        /* extensions like Server Name Indicatior (SNI).                     */
        const long flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
            SSL_OP_NO_COMPRESSION;
        SSL_CTX_set_options(ctx, flags);
        
        /* http://www.openssl.org/docs/ssl/SSL_CTX_load_verify_locations.html*/
        if (_hostChainPemFile)
        {
            int res = SSL_CTX_load_verify_locations(
                                                 ctx, _hostChainPemFile, NULL);
            ssl_err = ERR_get_error();
        
            if(!(1 == res))
            {
                /* Non-fatal, but something else will probably break later */
                /* break; */
            }
        }

        return(ctx);
    }
    while(true);

    // Broke out of loop => error
    if (ctx)
        SSL_CTX_free(ctx);
    
    return(NULL);
}


    

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------

SslAsync::SslAsync(const char * _hostName, unsigned int _hostPort,
                   const char * _hostChainPemFile) :
    mFd(-1),
    mWantsTcpRead(1),
    mWantsTcpWrite(1),
    mCallSslReadInsteadOfWrite(0),
    mCallSslWriteInsteadOfRead(0),
    mSsl(NULL), mCtxt(NULL)
{
    if (!_hostName)
    {
        errno = EINVAL;
        SSL_THROW("Null hostName");
    }

    if (!_hostChainPemFile)
    {
        errno = EINVAL;
        SSL_THROW("Null hostChainPemFile");
    }

    if (!_hostPort)
        _hostPort = 443;

    char buff[32];
    sprintf(&(buff[0]), "%u", _hostPort);

    struct addrinfo * addrinfo_ptr = NULL;
    int res = getaddrinfo(_hostName, &(buff[0]), NULL, &addrinfo_ptr);
    if (res != 0)
        SSL_THROW("local getaddrinfo failed");

    initOpenSslIfNotAlready();

    mCtxt = makeSslCtx(_hostChainPemFile);
    if (!mCtxt)
        SSL_THROW("could not SSL_CTX_new");

    mFd = socket(AF_INET, SOCK_STREAM, 0);
    if (mFd < 0)
        SSL_THROW("could not create socket");

    mSsl = SSL_new(mCtxt);
    if (!mSsl)
        SSL_THROW("could not SSL_new");

    // Set the socket to be non blocking.
    int flags = fcntl(mFd, F_GETFL, 0);
    if (fcntl(mFd, F_SETFL, flags | O_NONBLOCK))
        SSL_CLOSE_AND_THROW("could not fcntl");

    int one = 1;
    if (setsockopt(mFd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)))
        SSL_CLOSE_AND_THROW("could not setsockopt");

    mConnecting = 0;
    struct addrinfo * this_addrinfo;
    for(this_addrinfo = addrinfo_ptr; this_addrinfo;
        this_addrinfo = this_addrinfo->ai_next)
    {
        struct sockaddr * ai_addr = this_addrinfo->ai_addr;
      if (!ai_addr)
          continue;
      size_t ai_addrlen = this_addrinfo->ai_addrlen;
      if (!ai_addrlen)
          continue;
      
        int connect_res = connect(mFd, ai_addr, ai_addrlen);
        if (connect_res != -1)
        {
            errno = EINVAL;
            continue;
        }
        if (errno == EINPROGRESS)
        {
            mConnecting = 1; //true
            break;
        }
    }
    if (!mConnecting)
        SSL_CLOSE_AND_THROW("Failed to start connecting");

    if (!SSL_set_fd(mSsl, mFd))
        SSL_CLOSE_AND_THROW("could not SSL_set_fd");

    SSL_set_connect_state(mSsl);

    checkSocket(false/*not forAppRead*/);
}

// ---------------------------------------------------------------------------

SslAsync::~SslAsync()
{
    std::lock_guard<std::mutex> grd(mSslAsyncMutex);
    
    if (mSsl)
        SSL_free(mSsl);
    if (mCtxt)
        SSL_CTX_free(mCtxt);
    if (mFd > 0)
        close(mFd);
}

// ---------------------------------------------------------------------------

template<typename Duration>
void toTimeval(Duration&& d, struct timeval & tv)
{
    std::chrono::seconds const sec =
        std::chrono::duration_cast<std::chrono::seconds>(d);
    tv.tv_sec  = sec.count();
    tv.tv_usec =
        std::chrono::duration_cast<std::chrono::microseconds>(d - sec).count();
}

#define MAX_LOOP_COUNT 100

// Re: bool _forAppRead. If the caller is not doing this for a user read, we
// avoid triggering a call to ssl_read since (in the case that ssl_rdead reads
// user data) the application would have no way of knowing that we have read
// user data waiting and so may never call sslAppRecv to retrieve it

void SslAsync::checkSocket(bool _forAppRead)
{
    fd_set read_fds, write_fds;
    int select_res = 0;

    std::chrono::milliseconds wait_duration(0);
    std::chrono::milliseconds total_wait(0);

    struct timeval timeout;
    memset(&timeout, 0, sizeof(timeout));
    bool increase_timeout = false;

    unsigned int loop_count = 0;
    for(; loop_count < MAX_LOOP_COUNT; loop_count++)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        if (mWantsTcpRead) {
            FD_SET(mFd, &read_fds);
        }

        bool write_pending = (mWantsTcpWrite || mToWriteVec.size());
        if (write_pending)
            FD_SET(mFd, &write_fds);

        if (increase_timeout)
        {
            if (wait_duration <= std::chrono::milliseconds(0))
                    wait_duration = std::chrono::milliseconds(8);
                else
                    wait_duration = wait_duration * 2;
        }
        else
        {
            wait_duration = std::chrono::milliseconds(0);
        }

        toTimeval(wait_duration, timeout);

        bool assume_read_select = ((loop_count == 0) && _forAppRead &&
                              (!mCallSslWriteInsteadOfRead) && (!mConnecting));
        // We don't need to call select when checkSocket called with forAppRead
        // true, since caller must have seen a poll/select on the socket
        // already in order to call us
        
        // rptr to avoid reading when not wanted (see earlier comment re: bool
        // _forAppRead)
        fd_set * read_fs_rptr = ((!assume_read_select) &&
                  (_forAppRead || mConnecting || mCallSslWriteInsteadOfRead)) ?
                                &read_fds : NULL;
        fd_set * write_fs_rptr = ((!assume_read_select) &&
               (_forAppRead || mConnecting || (!mCallSslReadInsteadOfWrite))) ?
                                 &write_fds : NULL;
        
        select_res = assume_read_select ? 1 :
                                   select(mFd + 1, read_fs_rptr, write_fs_rptr,
                                          NULL, &timeout);
        if (select_res)
        {
            increase_timeout = false;
            
            if (assume_read_select ||
                (read_fs_rptr && FD_ISSET(mFd, &read_fds)))
            {
                if (mConnecting) {
                    ACTION action = ssl_connect();
                    if (action == CONTINUE)
                        continue;
                    else if (action == BREAK)
                        break;
                } else {
                    ACTION action;
                    if (mCallSslWriteInsteadOfRead)
                    {
                        action = ssl_write();
                    }
                    else
                    {
                        action = ssl_read();
                        if (action == NEITHER)
                        { // user read action succeeded
                            // If no wish for a write nor SSL-level read, break
                            // Note: !_forAppRead => write or connect, so
                            // !((!_forAppRead) || mConnecting) => not write
                            if (!((!_forAppRead) || mConnecting ||
                                  mCallSslWriteInsteadOfRead ||
                                  mCallSslReadInsteadOfWrite))
                                break;
                        }
                    }

                    if (action == CONTINUE) {
                        continue;
                    } else if (action == BREAK) {
                        break;
                    }
                }
            }

            if (write_fs_rptr && FD_ISSET(mFd, &write_fds)) {
                if (mConnecting) {
                    mWantsTcpWrite = 0;

                    ACTION action = ssl_connect();
                    if (action == CONTINUE) {
                        continue;
                    } else if (action == BREAK) {
                        break;
                    }
                } else {
                    ACTION action;
                    if (mCallSslReadInsteadOfWrite) {
                        action = ssl_read();
                    } else {
                        action = ssl_write();
                        if (action == NEITHER)
                        { // action == NEITHER => user write succeeded
                            // If no wish for read nor SSL-level write, break
                            if (!(_forAppRead || mConnecting ||
                                  mCallSslWriteInsteadOfRead ||
                                  mCallSslReadInsteadOfWrite))
                                break;
                        }
                        
                    }

                    if (action == CONTINUE) {
                        continue;
                    } else if (action == BREAK) {
                        break;
                    }
                }
            }
        } else
        { // select_res = 0, sockets not available
            if (mCallSslWriteInsteadOfRead)
            {
                increase_timeout = true;
                continue;
            }

            if (!write_pending && !mConnecting)
                    break;
                
            if (mConnecting)
            {
                increase_timeout = true;
                continue;
            }
            else
            {   // User write is pending
                // Send it to ssl, even though ssl isn't ready for user data

                ACTION action = ssl_write();
                if (action == CONTINUE)
                { // ssl_write rets CONTINUE only for WANT_WRITE/READ cases
                    increase_timeout = true;
                    continue;
                }
                else if (action == BREAK)
                {
                    break;
                }
                else
                { // action == NEITHER => user write succeeded
                  // If no wish for read nor an SSL-level write, break out;
                  // otherwise reset timeout
                    if (!(_forAppRead || mConnecting ||
                     mCallSslWriteInsteadOfRead || mCallSslReadInsteadOfWrite))
                        break;
                    increase_timeout = false;
                }
            }
        }
        
    }

    if (loop_count >= MAX_LOOP_COUNT) // Should never happen
    {
        errno = ETIMEDOUT;
    }
}

// ---------------------------------------------------------------------------

#endif // of ifdef PIST_INCLUDE_SSL

// ---------------------------------------------------------------------------
