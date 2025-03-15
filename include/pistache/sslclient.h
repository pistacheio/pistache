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

#pragma once
#include <memory>
#include <vector>

#include <pistache/winornix.h> // for PST_SSIZE_T
#include <pistache/eventmeth.h> // Fd

namespace Pistache::Http::Experimental
{

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
                  int _domain, // AF_INET or AF_INET6
                  const std::string & _hostResource,//without host, w/o queries
                  bool _doVerification,
                  const std::string * _hostChainPemFile);

    // Note: read(...) removed since not used

    PST_SSIZE_T sslRawRecv(void * _buffer, size_t _length, bool _knowReadable);
    PST_SSIZE_T sslRawSend(const void * _buffer, size_t _length);

    Fd getFd() const; // rets FD of underlying socket, or < 0 if not open
    int close(); // as per posix close
};


} // namespace
