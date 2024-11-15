/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */


// Defines certain socket functions (operations on an em_socket_t) that we
// implement using the corresponding winsock2 methods.

#include <pistache/pist_sockfns.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <errno.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <iostream>

#include <winsock2.h>

#include <pistache/pist_syslog.h>
#include <pistache/pist_check.h>

/* ------------------------------------------------------------------------- */

inline SOCKET get_win_socket_from_em_socket_t(em_socket_t ems)
{ return((ems < 0) ? INVALID_SOCKET : (static_cast<SOCKET>(ems))); }
// Note: SOCKET is some kind of unsigned integer

/* ------------------------------------------------------------------------- */

// Calls WSAGetLastError, sets errno and then returns -1 always
static int WSAGetLastErrorSetErrno()
{
    int wsa_last_err = WSAGetLastError();

    switch(wsa_last_err)
    {
    case WSANOTINITIALISED:
        PS_LOG_DEBUG("WSANOTINITIALISED");
        errno = EINVAL;
        break;

    case WSAENETDOWN:
        PS_LOG_DEBUG("WSAENETDOWN");
        errno = ENETDOWN;
        break;

    case WSAENOTSOCK:
        PS_LOG_DEBUG("WSAENOTSOCK");
        errno = ENOTSOCK;
        break;

    case WSAEINPROGRESS:
        PS_LOG_DEBUG("WSAEINPROGRESS");
        errno = EINPROGRESS;
        break;

    case WSAEALREADY:
        PS_LOG_DEBUG("WSAEALREADY");
        errno = EALREADY;
        break;

    case WSAENETUNREACH:
        PS_LOG_DEBUG("WSAENETUNREACH");
        errno = ENETUNREACH;
        break;

    case WSAEINTR:
        PS_LOG_DEBUG("WSAEINTR");
        errno = EINTR;
        break;

    case WSAECONNREFUSED:
        PS_LOG_DEBUG("WSAECONNREFUSED");
        errno = ECONNREFUSED;
        break;

    case WSAEISCONN:
        PS_LOG_DEBUG("WSAEISCONN");
        errno = EISCONN;
        break;

    case WSAEWOULDBLOCK:
        PS_LOG_DEBUG("WSAEWOULDBLOCK");
        errno = EWOULDBLOCK;
        break;

    case WSAEFAULT:
        PS_LOG_DEBUG("WSAEFAULT");
        errno = EFAULT;
        break;

    case WSAENOTCONN:
        PS_LOG_DEBUG("WSAENOTCONN");
        errno = ENOTCONN;
        break;

    case WSAENETRESET:
        PS_LOG_DEBUG("WSAENETRESET");
        errno = ENETRESET;
        break;

    case WSAESHUTDOWN:
        PS_LOG_DEBUG("WSAESHUTDOWN");
        errno = ECONNABORTED; // ESHUTDOWN not defined in Windows' errno.h
        break;

    case WSAEMSGSIZE:
        PS_LOG_DEBUG("WSAEMSGSIZE");
        errno = EMSGSIZE;
        break;

    case WSAEINVAL:
        PS_LOG_DEBUG("WSAEINVAL");
        errno = EINVAL;
        break;

    case WSAECONNABORTED:
        PS_LOG_DEBUG("WSAECONNABORTED");
        errno = ECONNABORTED;
        break;

    case WSAETIMEDOUT:
        PS_LOG_DEBUG("WSAETIMEDOUT");
        errno = ETIMEDOUT;
        break;

    case WSAECONNRESET:
        PS_LOG_DEBUG("WSAECONNRESET");
        errno = ECONNRESET;
        break;

    case WSAEACCES:
        PS_LOG_DEBUG("WSAEACCES");
        errno = EACCES;
        break;

    case WSAENOBUFS:
        PS_LOG_DEBUG("WSAENOBUFS");
        errno = ENOBUFS;
        break;

    case WSAEHOSTUNREACH:
        PS_LOG_DEBUG("WSAEHOSTUNREACH");
        errno = EHOSTUNREACH;
        break;

    case WSAEAFNOSUPPORT:
        PS_LOG_DEBUG("WSAEAFNOSUPPORT");
        errno = EAFNOSUPPORT;
        break;

    case WSAEMFILE:
        PS_LOG_DEBUG("WSAEMFILE");
        errno = EMFILE;
        break;

    case WSAEPROTONOSUPPORT:
        PS_LOG_DEBUG("WSAEPROTONOSUPPORT");
        errno = EPROTONOSUPPORT;
        break;

    case WSAEPROTOTYPE:
        PS_LOG_DEBUG("WSAEPROTOTYPE");
        errno = EPROTOTYPE;
        break;

    case WSAEPROVIDERFAILEDINIT:
        PS_LOG_DEBUG("WSAEPROVIDERFAILEDINIT");
        errno = EIO;
        break;

    case WSAESOCKTNOSUPPORT:
        PS_LOG_DEBUG("WSAESOCKTNOSUPPORT");
        errno = EPROTONOSUPPORT; // No ESOCKTNOSUPPORT in Windows' errno.h
        break;

    case WSAEADDRINUSE:
        PS_LOG_DEBUG("WSAEADDRINUSE");
        errno = EADDRINUSE;
        break;

    case WSAEADDRNOTAVAIL:
        PS_LOG_DEBUG("WSAEADDRNOTAVAIL");
        errno = EADDRNOTAVAIL;
        break;

    case WSAEOPNOTSUPP:
        PS_LOG_DEBUG("WSAEOPNOTSUPP");
        errno = EOPNOTSUPP;
        break;

    default:
        PS_LOG_DEBUG_ARGS("Unexpected WSA error %d:", wsa_last_err);
        errno = EIO;
        break;
    }

    return(-1);
}

/* ------------------------------------------------------------------------- */

std::atomic_bool lWsaStartupDone = false;
std::mutex lWsaStartupDoneMutex;

// WsaStartupAndCleanup exists solely so it's destructor can be called on
// program shutdown, allowing us to free winsock resources
class WsaStartupAndCleanup
{
public:
    WsaStartupAndCleanup() : inited(true) {};
    ~WsaStartupAndCleanup();

private:
    bool inited;
};
static WsaStartupAndCleanup lWsaStartupAndCleanup;

// pist_sock_startup_check must be called before any winsock2 function. It can
// be called as many times as you like, it does nothing after the first time it
// is called, and it is threadsafe. All the pist_sock_xxx functions in this
// file call it themselves, so you don't need to call pist_sock_startup_check
// before calling pist_sock_socket for instance. However, if code outside of
// this file is calling winsock functions, that code must call
// pist_sock_startup_check, using the macro provided in winornix.h.
//
// Returns 0 on success, or -1 on failure with errno set.
#define PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR      \
    if (pist_sock_startup_check() < 0)                  \
        return(-1);                                     \

int pist_sock_startup_check()
{
    if (lWsaStartupDone)
        return(0);

    GUARD_AND_DBG_LOG(lWsaStartupDoneMutex);
    if (lWsaStartupDone)
        return(0);

    const WORD version_required = (2 * 0x100) + 2; // version 2.2
    WSADATA wsadata;
    memset(&wsadata, 0, sizeof(wsadata));

    int wsastartup_res = WSAStartup(version_required, &wsadata);
    if (wsastartup_res == 0)
    {
        lWsaStartupDone = true;
        return(0); // success
    }

    switch(wsastartup_res)
    {
    case WSASYSNOTREADY:
        PS_LOG_DEBUG("WSAStartup WSASYSNOTREADY");
        errno = ENETUNREACH;
        break;

    case WSAVERNOTSUPPORTED:
        PS_LOG_DEBUG("WSAStartup WSAVERNOTSUPPORTED");
        errno = EOPNOTSUPP;
        break;

    case WSAEINPROGRESS:
        PS_LOG_DEBUG("WSAStartup WSAEINPROGRESS");
        errno = EINPROGRESS;
        break;

    case WSAEPROCLIM:
        PS_LOG_DEBUG("WSAStartup WSAEPROCLIM");
        errno = EMFILE; // too many files
        break;

    case WSAEFAULT:
        PS_LOG_DEBUG("WSAStartup WSAEFAULT");
        errno = EFAULT;
        break;

    default:
        PS_LOG_DEBUG_ARGS("Unexpected WSAStartup error %d:", wsastartup_res);
        errno = EIO;
        break;
    }

    return(-1);
}

WsaStartupAndCleanup::~WsaStartupAndCleanup() // DO NOT LOG
{
    // Since this is the destructor of what is in fact a static instance, we
    // are careful not log here (we just use std::cout instead); the logging
    // object may already have been destroyed before this destructor is called

    if (!lWsaStartupDone)
        return;

    std::lock_guard<std::mutex> guard(lWsaStartupDoneMutex);
    if (!lWsaStartupDone)
        return;

    int wsa_cleanupres = WSACleanup();

    if (wsa_cleanupres == 0)
    {
        #ifdef DEBUG
        std::cout << "WsaStartupAndCleanup: WSACleanup success" << std::endl;
        #endif
        lWsaStartupDone = false;
        return;
    }

    #ifdef DEBUG
    std::cout << "WsaStartupAndCleanup: WSACleanup fail, ret " <<
        wsa_cleanupres << std::endl;
    #endif
}

/* ------------------------------------------------------------------------- */

// Returns 0 for success. On fail, rets -1, and errno is set
int pist_sock_getsockname(em_socket_t em_sock,
                          struct sockaddr *addr, PST_SOCKLEN_T *addrlen)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int getsockname_res = ::getsockname(win_sock, addr, addrlen);
    if (getsockname_res == 0)
        return(0); // success

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// pist_sock_xxx fns ret 0 for success. On fail, ret -1, and errno is set

int pist_sock_close(em_socket_t em_sock)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int closesocket_res = ::closesocket(win_sock);
    if (closesocket_res == 0)
        return(0); // success

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */
// On success, returns number of bytes read (zero meaning the connection has
// gracefully closed). On failure, -1 is returned and errno is set

PST_SSIZE_T pist_sock_read(em_socket_t em_sock, void *buf, size_t count)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int recv_res = ::recv(win_sock, reinterpret_cast<char *>(buf),
                          static_cast<int>(count), 0 /* flags*/);
    if (recv_res != SOCKET_ERROR)
        return(recv_res); // success - return bytes read

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */
// On success, returns number of bytes written. On failure, -1 is returned and
// errno is set. Note that, even on success, bytes written may be fewer than
// count.

PST_SSIZE_T pist_sock_write(em_socket_t em_sock, const void *buf, size_t count)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int send_res = ::send(win_sock, reinterpret_cast<const char *>(buf),
                          (static_cast<int>(count)), 0/*flags*/);
    if (send_res != SOCKET_ERROR)
        return(send_res); // success - return bytes read

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */
// On success, returns em_socket_t. On failure, -1 is returned and errno is
// set.

em_socket_t pist_sock_socket(int domain, int type, int protocol)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET socket_res = ::socket(domain, type, protocol);
    if (socket_res != INVALID_SOCKET)
    {
        em_socket_t res = static_cast<em_socket_t>(socket_res);
        return(res);
    }

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_bind(em_socket_t em_sock, const struct sockaddr *addr,
                   PST_SOCKLEN_T addrlen)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int bind_res = ::bind(win_sock, addr, addrlen);
    if (bind_res != SOCKET_ERROR)
        return(0); // success - return bytes read

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success returns an em_socket_t for the accepted socket. On failure, -1 is
// returned and errno is set.
em_socket_t pist_sock_accept(em_socket_t em_sock, struct sockaddr *addr,
                             PST_SOCKLEN_T *addrlen)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    SOCKET accept_res = ::accept(win_sock, addr, addrlen);

    if (accept_res != INVALID_SOCKET)
    {
        em_socket_t res = static_cast<em_socket_t>(accept_res);
        return(res); // success - return em_socket_t for the accepted socket
    }

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_connect(em_socket_t em_sock, const struct sockaddr *addr,
                      PST_SOCKLEN_T addrlen)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    SOCKET connect_res = ::connect(win_sock, addr, addrlen);

    if (connect_res != INVALID_SOCKET)
        return(0); // success

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_listen(em_socket_t em_sock, int backlog)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    SOCKET listen_res = ::listen(win_sock, backlog);

    if (listen_res != INVALID_SOCKET)
        return(0); // success

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success, returns a nonnegative value which is the number of elements in
// the pollfds whose revents fields have been set to a non‐ zero value
// (indicating an event or an error).  A return value of zero indicates that
// the system call timed out before any file descriptors became read.
// On error, -1 is returned, and errno is set.
int pist_sock_poll(PST_POLLFD_T * fds, PST_NFDS_T nfds, int timeout)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    if (!nfds)
    {
        PS_LOG_DEBUG("Zero nfds");
        return(0);
    }

    if (!fds)
    {
        PS_LOG_INFO("Null fds");
        errno = EINVAL;
        return(-1);
    }

    std::vector<WSAPOLLFD> win_fds_vec(nfds);
    for(unsigned int i=0; i<nfds; ++i)
    {
        win_fds_vec[i].fd = get_win_socket_from_em_socket_t(fds[i].fd);
        win_fds_vec[i].events = fds[i].events;
        win_fds_vec[i].revents = 0;
    }

    int win_poll_res = WSAPoll(win_fds_vec.data(), nfds, timeout);
    if (win_poll_res == 0)
        return(0); // Success, but no events before timer expired

    if (win_poll_res > 0)
    {
        for(unsigned int i=0; i<nfds; ++i)
            fds[i].revents = win_fds_vec[i].revents;

        return(win_poll_res); // Success, ret number of events with revent set
    }

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

PST_SSIZE_T pist_sock_send(em_socket_t em_sock, const void *buf,
                           size_t len, int flags)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int send_res = ::send(win_sock, reinterpret_cast<const char *>(buf),
                          static_cast<int>(len), flags);

    if (send_res != SOCKET_ERROR)
        return(send_res); // success - ret number of bytes sent

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

// On success, returns the number of bytes received. On error, -1 is
// returned and errno is set. Returns 0 if connection closed gracefully.
PST_SSIZE_T pist_sock_recv(em_socket_t em_sock, void * buf, size_t len,
                           int flags)
{
    PIST_SOCK_STARTUP_CHECK_RET_MINUS_1_ON_ERR;

    SOCKET win_sock = get_win_socket_from_em_socket_t(em_sock);
    if (win_sock == INVALID_SOCKET)
    {
        PS_LOG_DEBUG("Invalid Socket");
        errno = EBADF;
        return(-1);
    }

    int recv_res = ::recv(win_sock, reinterpret_cast<char *>(buf),
                          static_cast<int>(len), flags);

    if (recv_res != SOCKET_ERROR)
        return(recv_res); // success - ret number of bytes received

    return(WSAGetLastErrorSetErrno());
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
