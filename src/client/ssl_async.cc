/*
 * SPDX-FileCopyrightText: 2017, 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * ssl_async.cc
 *
 * The above SPDX-FileCopyrightText refers to changes and extensions made to
 * the sample code from which this code derives. With respect to that prior
 * sample code itself:
 *
 * The use of sample code as follows is acknowledged:
 * https://codereview.stackexchange.com/questions/108600/
 *                                               complete-async-openssl-example
 * Sample code license: Code on stackexchange is licensed under the MIT
 * license, except that the notice provisions of the MIT license are waived
 * absent a request of the code contributor or of StackExchange. In this case,
 * we do not see any such request, but the terms of the MIT license are
 * nonetheless hereby acknowledged.
 * Non-code contributions (explanations etc.) on stackexchange are permissively
 * licensed under the Creative Commons license (CC BY-SA 3.0)
 *
 * The use of sample code provided by openssl.org is likewise acknowledged:
 *   Model implementations at openssl.org in Openssl-bio-fetch.tar.gz at
 *   https://wiki.openssl.org/index.php/SSL/TLS_Client.
 * Sample code license: openssl permits modifications redistributions and use,
 * provided certain notices are retained and acknowledgements made
 * (https://www.openssl.org/source/license.html)
 *
 */

#include <pistache/winornix.h>

#include <mutex>

#include <stdio.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/conf.h> // for OPENSSL_config

#include <pistache/emosandlibevdefs.h> // for _USE_LIBEVENT_LIKE_APPLE
#include <pistache/ssl_async.h>
#include <pistache/pist_syslog.h>
#include <pistache/pist_timelog.h>

// For GENERAL_NAMES stuff
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <fcntl.h>
#include <sys/types.h>
#include PST_SOCKET_HDR
#include PST_NETDB_HDR // getaddrinfo, getprotobyname
#include PST_NETINET_TCP_HDR
#include PST_SELECT_HDR
#include PST_NETINET_IN_HDR
#include PST_MISC_IO_HDR // for close

#include <pistache/pist_sockfns.h>

// ---------------------------------------------------------------------------

namespace Pistache::Http::Experimental {

// ---------------------------------------------------------------------------

#define SSL_LOG_WRN_AND_THROW(__MSG)            \
    {                                           \
        PS_LOG_WARNING(__MSG);                      \
        throw std::runtime_error(__MSG);        \
    }

#define SSL_LOG_WRN_CLOSE_AND_THROW(__MSG)            \
    {                                                 \
        PS_LOG_WARNING(__MSG);                            \
        auto current_fd = mFd;                        \
        mFd = PS_FD_EMPTY;                            \
        CLOSE_FD(current_fd);                         \
        throw std::runtime_error(__MSG);              \
    }

// ---------------------------------------------------------------------------

static bool lOpenSslInited = false;
static std::mutex lOpenSslInitedMutex;

// ---------------------------------------------------------------------------

typedef enum {
  CONTINUE,
  BREAK,
  NEITHER
} ACTION;

// ---------------------------------------------------------------------------

PST_SSIZE_T SslAsync::sslAppSend(const void * _buffer, size_t _length)
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
        int check_socket_res = checkSocket(false/*not forAppRead*/);
        if (check_socket_res)
            return(-1); // errno already set by checkSocket
        if (!mToWriteVec.size())
            return(_length);

        if (mToWriteVec.size() >= starting_size)
            break;

        total_written += (starting_size - mToWriteVec.size());
    }
    if ((loop_count >= 256) || (total_written <= prior_size))
    { // No write happened this time. Let go of this data, return error
        mToWriteVec.resize(prior_size - total_written);
        PS_LOG_INFO("Failed to send queued app write via SSL");
        errno = EWOULDBLOCK;
        return(-1);
    }

    return(total_written - prior_size);
}

// ---------------------------------------------------------------------------

SslAsync::ACTION SslAsync::ssl_connect()
{
    PS_LOG_DEBUG_ARGS("calling SSL_connect for mSsl %p", mSsl);

    int result = SSL_connect(mSsl);

    if (result <= 0)
    {
        int ssl_error = SSL_get_error(mSsl, result);

        if (result == 0)
        {
            // Note: Per openssl documentation, ssl_error should NOT be one of
            // the SSL_ERROR_WANT_xxx values when result is zero. Also, zero
            // means: "The TLS/SSL handshake was not successful but was shut
            // down controlled and by the specifications of TLS/SSL"
            const char* error_string = ERR_error_string(ssl_error, NULL);
            PS_LOG_INFO_ARGS("Could not SSL_connect ssl_err %d; %s",
                             ssl_error, error_string);
        }
        else
        { // result < 0

            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                PS_LOG_DEBUG("SSL_connect wants write");
                mWantsTcpWrite = 1;
                return CONTINUE;
            }

            if (ssl_error == SSL_ERROR_WANT_READ) {
                PS_LOG_DEBUG("SSL_connect wants read");
                // wants_tcp_read is always 1;
                return CONTINUE;
            }

            if (ssl_error == SSL_ERROR_WANT_RETRY_VERIFY) {
                PS_LOG_DEBUG("SSL_ERROR_WANT_RETRY_VERIFY");
                return CONTINUE;
            }

            #ifdef DEBUG
            const char* error_string = ERR_error_string(ssl_error, NULL);
            PS_LOG_DEBUG_ARGS("Could not SSL_connect ssl_err %d; %s",
                              ssl_error, error_string);
            #endif
        }
        return BREAK;
    }

    PS_LOG_DEBUG("SSL connected");
    mConnecting = 0;
    return BREAK; // Was previously CONTINUE
}

// ---------------------------------------------------------------------------

PST_SSIZE_T SslAsync::sslAppRecv(void * _buffer, size_t _length)
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

    PST_SSIZE_T bytes_received = std::min(_length, mReadFromVec.size());

    memcpy(_buffer, mReadFromVec.data(), bytes_received);
    mReadFromVec.erase(mReadFromVec.begin(),
                       mReadFromVec.begin() + bytes_received);
    return(bytes_received);
}

// ---------------------------------------------------------------------------

SslAsync::ACTION SslAsync::ssl_read()
{
  PS_LOG_DEBUG("calling SSL_read");

  mCallSslReadForSslLib = 0;

  char buffer[1536];
  int num = SSL_read(mSsl, buffer, sizeof(buffer));

  if (num <= 0) {
    int ssl_error = SSL_get_error(mSsl, num);
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
      PS_LOG_DEBUG("SSL_read wants write");
      mWantsTcpWrite = 1;
      mCallSslWriteForSslLib = 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_WANT_READ) {
      PS_LOG_DEBUG("SSL_read wants read");
      // wants_tcp_read is always 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_ZERO_RETURN) {
      PS_LOG_DEBUG("Peer closed the TLS/SSL connection for writing");
      // Can also happen if peer abruptly closed the connection and we have
      // SSL_OP_IGNORE_UNEXPECTED_EOF set
    } else {
        #ifdef DEBUG
        long error =
        #endif
            ERR_get_error();
        #ifdef DEBUG
        const char* error_string = ERR_error_string(error, NULL);
        PS_LOG_DEBUG_ARGS("Could not SSL_read (returned <= 0), "
                          "ssl_error %d, ERR %ld (%s)",
                          ssl_error,
                          error, error_string);
        #endif
    }

    // ECONNRESET: Connection reset by peer; or else, no data available
    errno = ((ssl_error == SSL_ERROR_ZERO_RETURN) ? ECONNRESET : ENODATA);
    return BREAK;
  } else {
      mReadFromVec.insert(mReadFromVec.end(), &(buffer[0]), &(buffer[num]));

      PS_LOG_DEBUG_ARGS("read %d bytes", num);
      /*
       * Use this one instead to output the whole read buffer
      PS_LOG_DEBUG_ARGS("read %d bytes: %s", num, &(buffer[0]));
      */
  }

  return NEITHER;
}

// ---------------------------------------------------------------------------


SslAsync::ACTION SslAsync::ssl_write()
{
  PS_LOG_DEBUG("calling SSL_write");

  if (mCallSslWriteForSslLib && (mToWriteVec.empty())) {
    PS_LOG_INFO("ssl should not have requested a write from a read if no data was waiting to be written");
    return BREAK;
  }

  mCallSslWriteForSslLib = 0;

  if (mToWriteVec.empty())
    return NEITHER;

  int num = SSL_write(mSsl, mToWriteVec.data(),
                      static_cast<int>(mToWriteVec.size()));
  if (num == 0) {
    long error = ERR_get_error();
    const char* error_str = ERR_error_string(error, NULL);
    PS_LOG_INFO_ARGS("could not SSL_write (returned 0): %s", error_str);
    return BREAK;
  } else if (num < 0) {
    int ssl_error = SSL_get_error(mSsl, num);
    if (ssl_error == SSL_ERROR_WANT_WRITE) {
      PS_LOG_DEBUG("SSL_write wants write");
      mWantsTcpWrite = 1;
      return CONTINUE;
    }

    if (ssl_error == SSL_ERROR_WANT_READ) {
      PS_LOG_DEBUG("SSL_write wants read");
      mCallSslReadForSslLib = 1;
      // wants_tcp_read is always 1;
      return CONTINUE;
    }

#ifdef DEBUG
    long error =
#endif
        ERR_get_error();
#ifdef DEBUG
    const char* error_string = ERR_error_string(error, NULL);
    PS_LOG_DEBUG_ARGS("could not SSL_write (returned -1): %s", error_string);
#endif
    return BREAK;
  } else {
      PS_LOG_DEBUG_ARGS("wrote %d of %d bytes", num, mToWriteVec.size());
      if (mToWriteVec.size() < static_cast<std::size_t>(num))
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
    // OPENSSL_config is deprecated. CONF_modules_load() could be called
    // instead if needed.
    //
    // OPENSSL_config(NULL);
    /* Cannot fail ??? */

    /* Include <openssl/opensslconf.h> to get this define     */
#if defined (OPENSSL_THREADS)
    /* TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO */
    /* https://www.openssl.org/docs/crypto/threads.html */
    PS_LOG_DEBUG("Warning: thread locking is not implemented");
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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

        PS_LOG_DEBUG_ARGS("%s: %s", label, utf8);
        success = 1;

    } while (0);

    if(utf8)
        OPENSSL_free(utf8);

    if(!success)
        PS_LOG_INFO_ARGS("%s: <not available>", label);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void logSanName([[maybe_unused]]const char* label, X509* const cert)
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

                if(len1 != len2) {
                    PS_LOG_INFO_ARGS("Strlen and ASN1_STRING size do not"
                            " match (embedded null?): %d vs %d", len2, len1);
                }

                /* If there's a problem with string lengths, then     */
                /* we skip the candidate and move on to the next.     */
                /* Another policy would be to fails since it probably */
                /* indicates the client is under attack.              */
                if(utf8 && len1 && len2 && (len1 == len2)) {
                    PS_LOG_DEBUG_ARGS("%s: %s", label, utf8);
                    success = 1;
                }

                if(utf8) {
                    OPENSSL_free(utf8), utf8 = NULL;
                }
            }
            else
            {
                PS_LOG_INFO_ARGS("Unknown GENERAL_NAME type: %d",
                                 entry->type);
            }
        }

    } while (0);

    if(names)
        GENERAL_NAMES_free(names);

    if(utf8)
        OPENSSL_free(utf8);

    if(!success)
        PS_LOG_DEBUG_ARGS("%s: <not available>", label);

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx)
{
    /* For error codes, see http://www.openssl.org/docs/apps/verify.html  */

    int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
    int err = X509_STORE_CTX_get_error(x509_ctx);

    SSL * ssl = reinterpret_cast<SSL *>(X509_STORE_CTX_get_ex_data(
                                        x509_ctx,
                                        SSL_get_ex_data_X509_STORE_CTX_idx()));

    if (!preverify_ok)
    {
        bool * do_verification_ptr =
            reinterpret_cast<bool *>(SSL_get_ex_data(ssl,0));
        if (!do_verification_ptr)
        {
            PS_LOG_INFO("not preverified, yet do_verification_ptr is null");
            return preverify_ok;
        }
        if (*do_verification_ptr)
        {
            if(err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
                PS_LOG_INFO(
                    "X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY");
            else if(err == X509_V_ERR_CERT_UNTRUSTED)
                PS_LOG_INFO("X509_V_ERR_CERT_UNTRUSTED");
            else if(err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)
                PS_LOG_INFO("X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN");
            else if(err == X509_V_ERR_CERT_NOT_YET_VALID)
                PS_LOG_INFO("X509_V_ERR_CERT_NOT_YET_VALID");
            else if(err == X509_V_ERR_CERT_HAS_EXPIRED)
                PS_LOG_INFO("X509_V_ERR_CERT_HAS_EXPIRED");
            else if(err == X509_V_OK)
                PS_LOG_INFO("X509_V_OK");
            else if(err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)
                PS_LOG_INFO("X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT");
            else
                PS_LOG_INFO_ARGS("Error = %d", err);

            return preverify_ok;
        }

        PS_LOG_DEBUG_ARGS("X509 code = %d; verification off", err);
        return 1;
    }

    X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);
    X509_NAME* iname = cert ? X509_get_issuer_name(cert) : NULL;
    X509_NAME* sname = cert ? X509_get_subject_name(cert) : NULL;

    PS_LOG_DEBUG_ARGS("verify_callback (depth=%d)(preverify_ok=%d)",
                     depth, preverify_ok);

    /* Issuer is the authority we trust that warrants nothing useful */
    logCnName("Issuer (cn)", iname);

    /* Subject is who the certificate is issued to by the authority  */
    logCnName("Subject (cn)", sname);

    if(depth == 0) {
        /* If depth is 0, its the server's certificate. Log the SANs */
        logSanName("Subject (san)", cert);
    }

    return preverify_ok;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static SSL_CTX * makeSslCtx(const char * _hostChainPemFile)
{
    if ((!_hostChainPemFile) || (!strlen(_hostChainPemFile)))
    {
        PS_LOG_DEBUG("_hostChainPemFile is empty");
        errno = EINVAL;
        return(NULL);
    }

    SSL_CTX * ctx = NULL;

    initOpenSslIfNotAlready();

    do {
        /* https://www.openssl.org/docs/ssl/SSL_CTX_new.html */
        const SSL_METHOD* method = SSLv23_method();

        #ifdef DEBUG
        unsigned long ssl_err = ERR_get_error();
        if(!(NULL != method))
        {
            PS_LOG_DEBUG_ARGS("SSLv23_method ssl_err: %d", ssl_err);
            break; /* failed */
        }
        #else
        ERR_get_error();
        #endif

        /* http://www.openssl.org/docs/ssl/ctx_new.html */
        ctx = SSL_CTX_new(method);
        /* ctx = SSL_CTX_new(TLSv1_method()); */

        if(!(ctx != NULL))
        {
            PS_LOG_DEBUG_ARGS("SSL_CTX_new ssl_err: %d", ssl_err);
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
        const uint64_t flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
            SSL_OP_NO_COMPRESSION
            /*
             * We could do SSL_OP_IGNORE_UNEXPECTED_EOF, but it seems we don't
             * need to (Jan/2025)
            #ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
            // Allows the connection to close cleanly even if the peer does not
            // send a close_notify alert prior to doing the close
            // SSL_OP_IGNORE_UNEXPECTED_EOF introduced in OpenSSL 3.0
            | SSL_OP_IGNORE_UNEXPECTED_EOF
            #endif
            */
            ;

        SSL_CTX_set_options(ctx, flags);

        /* http://www.openssl.org/docs/ssl/SSL_CTX_load_verify_locations.html*/
        if (_hostChainPemFile)
        {
            int res = SSL_CTX_load_verify_locations(
                                                 ctx, _hostChainPemFile, NULL);
            unsigned long locn_ssl_err = ERR_get_error();

            if(!(1 == res))
            {
                /* Non-fatal, but something else will probably break later */
                PS_LOG_INFO_ARGS(
                    "SSL_CTX_load_verify_locations locn_ssl_err: %d",
                    locn_ssl_err);
                /* break; */
            }
        }

        return(ctx);
    }
    while(true);

    PS_LOG_DEBUG_ARGS("Broke out of loop => error, ctx %p", ctx);
    if (ctx)
        SSL_CTX_free(ctx);

    return(NULL);
}

// ---------------------------------------------------------------------------

SslAsync::SslAsync(const char * _hostName, unsigned int _hostPort,
                   int _domain, // AF_INET or AF_INET6
                   bool _doVerification,
                   const char * _hostChainPemFile) :
    mFd(PS_FD_EMPTY),
    mWantsTcpRead(1),
    mWantsTcpWrite(1),
    mCallSslReadForSslLib(0),
    mCallSslWriteForSslLib(0),
    mDoVerification(_doVerification),
    mSsl(NULL), mCtxt(NULL)
{
    if (!_hostName)
    {
        errno = EINVAL;
        SSL_LOG_WRN_AND_THROW("Null hostName");
    }

    if (!_hostChainPemFile)
    {
        errno = EINVAL;
        SSL_LOG_WRN_AND_THROW("Null hostChainPemFile");
    }

    if (!_hostPort)
        _hostPort = 443;

    std::string host_port_as_sstr(std::to_string(_hostPort));
    struct addrinfo * addrinfo_ptr = NULL;
    PS_LOG_DEBUG_ARGS("Doing getaddrinfo. _hostName %s, _hostPort %u",
                      _hostName, static_cast<unsigned int>(_hostPort));
    int res = getaddrinfo(_hostName, host_port_as_sstr.c_str(),
                          NULL, &addrinfo_ptr);
    PS_LOG_DEBUG_ARGS("getaddrinfo res %d", res);
    if (res != 0)
        SSL_LOG_WRN_AND_THROW("local getaddrinfo failed");

    initOpenSslIfNotAlready();

    mCtxt = makeSslCtx(_hostChainPemFile);
    if (!mCtxt)
        SSL_LOG_WRN_AND_THROW("could not SSL_CTX_new");

    em_socket_t sfd = PST_SOCK_SOCKET(_domain, SOCK_STREAM, 0);
    if (sfd < 0)
        SSL_LOG_WRN_AND_THROW("could not create socket");

    #ifdef _USE_LIBEVENT
    // We're openning a connection to a remote resource - I guess
    // it makes sense to allow either read or write
    mFd = TRY_NULL_RET(EventMethFns::em_event_new(
                           sfd, // pre-allocated file desc
                           EVM_READ | EVM_WRITE | EVM_PERSIST | EVM_ET,
                           F_SETFDL_NOTHING, // setfd
                           PST_O_NONBLOCK // setfl
                           ));
    #else
    mFd = sfd;
    #endif

    mSsl = SSL_new(mCtxt);
    if (!mSsl)
        SSL_LOG_WRN_AND_THROW("could not SSL_new");

    // Set the socket to be non blocking.
    int flags = PST_FCNTL(sfd, PST_F_GETFL, 0);
    if (flags == PST_FCNTL_GETFL_UNKNOWN)
        flags = 0;
    if (PST_FCNTL(sfd, PST_F_SETFL, flags | PST_O_NONBLOCK))
        SSL_LOG_WRN_CLOSE_AND_THROW("could not fcntl");

    #ifdef _USE_LIBEVENT_LIKE_APPLE
      // SOL_TCP not defined in macOS Nov 2023
      const struct protoent* pe = getprotobyname("tcp");
      int tcp_prot_num          = pe ? pe->p_proto : 6;
    #ifdef DEBUG
    #ifdef __linux__
      assert(tcp_prot_num == SOL_TCP);
    #endif
    #endif
    #else
      int tcp_prot_num = SOL_TCP;
    #endif
    PST_SOCK_OPT_VAL_TYPICAL_T one = 1;
    if (::setsockopt(
            sfd, tcp_prot_num, TCP_NODELAY,
            reinterpret_cast<PST_SOCK_OPT_VAL_PTR_T>(&one),
            sizeof(one)))
        SSL_LOG_WRN_CLOSE_AND_THROW("could not setsockopt");

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

        int connect_res = PST_SOCK_CONNECT(sfd, ai_addr,
                                           static_cast<socklen_t>(ai_addrlen));
        PS_LOG_DEBUG_ARGS("Socket connect res = %d", connect_res);
        if (connect_res != -1)
        {
            PS_LOG_WARNING("Expecting non-blocking connect for SSL");
            errno = EINVAL;
            continue;
        }
        if (errno ==
        #ifdef _IS_WINDOWS
                     EWOULDBLOCK
        #else
                     EINPROGRESS
        #endif
            )
        {

            mConnecting = 1; //true
            break;
        }
    }
    PS_LOG_DEBUG_ARGS("mConnecting = %d", mConnecting);
    if (!mConnecting)
        SSL_LOG_WRN_CLOSE_AND_THROW("Failed to start connecting");

    // Save a pointer to mDoVerification for use in verify_callback
    SSL_set_ex_data(mSsl, 0, &mDoVerification); // 0 is "idx" for app data

    // Note: SSL_set_tlsext_host_name is required to get _hostName google.com
    // to work correctly for verify / verify_callback. Cf.:
    //     https://docs.openssl.org/3.2/man7/ossl-guide-tls-client-block/
    //                                            #setting-the-servers-hostname
    if (!SSL_set_tlsext_host_name(mSsl, _hostName))
        SSL_LOG_WRN_CLOSE_AND_THROW("could not SSL_set_tlsext_host_name");

    SSL_set_hostflags(mSsl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (!SSL_set1_host(mSsl, _hostName))
        SSL_LOG_WRN_CLOSE_AND_THROW("could not SSL_set1_host");

    if (!SSL_set_fd(mSsl,
                    #ifdef _IS_WINDOWS
                    // SSL_set_fd takes type int for the FD parm, resulting
                    // in a compiler warning since em_socket_t (and Windows'
                    // SOCKET) may be wider than "int". However, according
                    // to the SLL documentation, the warning can be
                    // suppressed / ignored. @Aug/2024, see 'NOTES' in:
                    // https://docs.openssl.org/3.1/man3/SSL_set_fd/
                    static_cast<int>(
                    #endif
                        sfd
                    #ifdef _IS_WINDOWS
                        )
                    #endif
            ))
        SSL_LOG_WRN_CLOSE_AND_THROW("could not SSL_set_fd");

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
    if (mFd != PS_FD_EMPTY)
        CLOSE_FD(mFd);
}

// ---------------------------------------------------------------------------

template<typename Duration>
void toTimeval(Duration&& d, struct timeval & tv)
{
    std::chrono::seconds const sec =
        std::chrono::duration_cast<std::chrono::seconds>(d);
    tv.tv_sec  = static_cast<PST_TIMEVAL_S_T>(sec.count());
    tv.tv_usec = static_cast<PST_SUSECONDS_T>
      (std::chrono::duration_cast<std::chrono::microseconds>(d - sec).count());
}

#define MAX_LOOP_COUNT 100

// Re: bool _forAppRead. If the caller is not doing this for a user read, we
// avoid triggering a call to ssl_read since (in the case that ssl_read reads
// user data) the application would have no way of knowing that we have read
// user data waiting and so may never call sslAppRecv to retrieve it
// Returns -1 when errno has been set, 0 otherwise
int SslAsync::checkSocket(bool _forAppRead)
{
    PS_TIMEDBG_START_ARGS("mConnecting %d, mDoVerification %s, "
                          "_forAppRead %s, "
                          "mWantsTcpRead %d, mWantsTcpWrite %d, "
                          "mCallSslReadForSslLib %d, "
                          "mCallSslWriteForSslLib %d",
                          mConnecting, mDoVerification ? "true" : "false",
                          _forAppRead ? "true" : "false",
                          mWantsTcpRead, mWantsTcpWrite,
                          mCallSslReadForSslLib,
                          mCallSslWriteForSslLib);

    fd_set read_fds, write_fds;
    int select_res = 0;

    const auto start_time = std::chrono::steady_clock::now();// monotonic clock
    const std::chrono::seconds max_total_wait(mConnecting? 65 : 45);

    em_socket_t sfd = GET_ACTUAL_FD(mFd);
    bool use_nonzero_timeout = mConnecting || mCallSslWriteForSslLib
        || mCallSslReadForSslLib;
    // Below, if use_nonzero_timeout is true, we wait for a result (or a
    // timeout) when we call "select"; if use_nonzero_timeout is false, select
    // returns immediately whether we have a result yet or not.

    int res = 0;

    unsigned int loop_count = 0;
    for(; loop_count < MAX_LOOP_COUNT; loop_count++)
    {
        PS_LOG_DEBUG("selecting");
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        if (mWantsTcpRead) {
            FD_SET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &read_fds);
        }

        bool write_pending = (mWantsTcpWrite || mToWriteVec.size());
        if (write_pending) {
            FD_SET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &write_fds);
        }

        const auto wait_so_far(std::chrono::steady_clock::now() - start_time);
        if (wait_so_far >= max_total_wait)
        {
            PS_LOG_DEBUG("SSL socket actions timed out");

            errno = ETIMEDOUT;
            res = -1;
            break;
        }

        const auto wait_duration = (max_total_wait - wait_so_far);

        PS_LOG_DEBUG_ARGS("Trying SSL after wait so far of %dms",
                          std::chrono::duration_cast<std::chrono::milliseconds>
                          (wait_so_far).count());

        bool assume_read_select = ((loop_count == 0) && _forAppRead &&
                              (!mCallSslWriteForSslLib) && (!mConnecting));
        // We don't need to call select when checkSocket called with forAppRead
        // true, since caller must have seen a poll/select on the socket
        // already in order to call us

        // rptr to avoid reading when not wanted (see earlier comment re: bool
        // _forAppRead)
        fd_set * read_fs_rptr = ((!assume_read_select) &&
               (_forAppRead || mConnecting || (!mCallSslWriteForSslLib))) ?
                                &read_fds : NULL;
        fd_set * write_fs_rptr = ((!assume_read_select) &&
               (_forAppRead || mConnecting || (!mCallSslReadForSslLib))) ?
                                 &write_fds : NULL;

        fd_set * except_fs_rptr = nullptr;
        #if _IS_WINDOWS
        fd_set except_fds;
        if ((write_fs_rptr) &&
            FD_ISSET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &write_fds))
        {
            FD_ZERO(&except_fds);
            FD_SET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &except_fds);
            except_fs_rptr = &except_fds;
        }
        #endif

        struct timeval timeout;
        if (use_nonzero_timeout)
            toTimeval(wait_duration, timeout);
        else
            memset(&timeout, 0, sizeof(timeout));

        // Per Linux and macOS man page, the first parm of select, nfds,
        // "should be set to the highest-numbered file descriptor in any of the
        // three sets, plus 1". In Windows, nfds is ignored.

        PS_LOG_DEBUG_ARGS("%s select, timeout.tv_sec %d, tv_usec %d, "
                          "mConnecting %s",
                          assume_read_select ? "Not calling" : "Calling",
                          static_cast<int>(timeout.tv_sec),
                          static_cast<int>(timeout.tv_usec),
                          mConnecting ? "true" : "false");
        select_res = assume_read_select ? 1 :
            PST_SOCK_SELECT(static_cast<int>(sfd) + 1,
                            read_fs_rptr, write_fs_rptr,
                            except_fs_rptr, &timeout);
        #if _IS_WINDOWS
        if ((except_fs_rptr) &&
            FD_ISSET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &except_fds) &&
            (!FD_ISSET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &write_fds)))
        {
            FD_SET(static_cast<PST_FD_SET_FD_TYPE>(sfd), &write_fds);
        }
        // In Windows, non-blocking sockets that fail connect are activated in
        // except_fds upon select, whereas in Linux they are activated in
        // write_fds. So we add activated socket from except_fds to write_fds
        // for Linux-like behavior. See e.g.:
        //   https://stackoverflow.com/questions/25369586
        #endif

        if (select_res)
        {
            use_nonzero_timeout = mConnecting;

            if (assume_read_select ||
                (read_fs_rptr && FD_ISSET(sfd, &read_fds)))
            {
                PS_LOG_DEBUG("readable");

                if (mConnecting) {
                    ACTION action = ssl_connect();
                    if (action == CONTINUE)
                        continue;
                    else if (action == BREAK)
                    {
                        errno = ECONNREFUSED;
                        res = -1;
                        break;
                    }

                } else {
                    ACTION action;
                    if (mCallSslWriteForSslLib)
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
                                  mCallSslWriteForSslLib ||
                                  mCallSslReadForSslLib))
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

            if (write_fs_rptr && FD_ISSET(sfd, &write_fds)) {
                PS_LOG_DEBUG("writable");

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
                    if (mCallSslReadForSslLib) {
                        action = ssl_read();
                    } else {
                        action = ssl_write();
                        if (action == NEITHER)
                        { // action == NEITHER => user write succeeded
                            // If no wish for read nor SSL-level write, break
                            if (!(_forAppRead || mConnecting ||
                                  mCallSslWriteForSslLib ||
                                  mCallSslReadForSslLib))
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
            if (mCallSslWriteForSslLib)
            {
                use_nonzero_timeout = true;
                continue;
            }

            if (!write_pending && !mConnecting)
                    break;

            if (mConnecting)
            {
                PS_LOG_DEBUG_ARGS(
                    "Socket not ready for SLL connect, "
                    "loop_count %u, wait_so_far %dms, "
                    "wait_duration %dms, continuing",
                    loop_count,
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (wait_so_far).count(),
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (wait_duration).count());

                use_nonzero_timeout = true;
                continue;
            }
            else
            {   // User write is pending
                // Send it to ssl, even though ssl isn't ready for user data
                PS_LOG_DEBUG_ARGS(
                    "Socket not ready for SSL user read or "
                    "write, loop_count %u, wait_so_far %dms, "
                    "wait_duration %dms, continuing",
                    loop_count,
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (wait_so_far).count(),
                    std::chrono::duration_cast<std::chrono::milliseconds>
                    (wait_duration).count());

                ACTION action = ssl_write();
                if (action == CONTINUE)
                { // ssl_write rets CONTINUE only for WANT_WRITE/READ cases
                    use_nonzero_timeout = true;
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
                     mCallSslWriteForSslLib || mCallSslReadForSslLib))
                        break;
                    use_nonzero_timeout = false;
                }
            }
        }
    }

    if (loop_count >= MAX_LOOP_COUNT)
    {
        PS_LOG_WARNING(
            "Looped too many times waiting for SSL socket - timeout");
        errno = ETIMEDOUT;
        res = -1;
    }
    return(res);
}

// ---------------------------------------------------------------------------

} // namespace Pistache::Http::Experimental
