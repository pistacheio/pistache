/******************************************************************************
 * sslclient.h
 *
 * C++ callable low-level SSL client capabilities.
 *
 */

#pragma once

#include "pistache/ssl_do_or_dont.h"
#ifdef PIST_INCLUDE_SSL

// ---------------------------------------------------------------------------

#include <memory>
#include <vector>

namespace Pistache {

namespace Http {

class SslConnectionImpl;

class SslConnection 
{
private:
    std::shared_ptr<SslConnectionImpl> mImpl; // never NULL

public:
    // _hostPort 0 => use default
    // If _hostChainPemFile is NULL, then then the authenticity of the server's
    // identity is not checked
    SslConnection(const std::string & _hostName,
                  unsigned int _hostPort, // zero => default
                  const std::string & _hostResource,//without host, w/o queries
                  const std::string * _hostChainPemFile);

    ssize_t sslRawRecv(void * _buffer, size_t _length);
    ssize_t sslRawSend(const void * _buffer, size_t _length);

    int getFd() const; // rets FD of underlying socket, or < 0 if not open
    int close(); // as per posix close
};
    
    
} // namespace Http

} // namespace Net

// ---------------------------------------------------------------------------

#endif // of ifdef PIST_INCLUDE_SSL

// ---------------------------------------------------------------------------


    
    
