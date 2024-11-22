/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines certain socket functions (operations on an em_socket_t) that we
// implement using the corresponding winsock2 methods.
//
// #include <pistache/pist_sockfns.h>

#ifndef _PIST_SOCKFNS_H_
#define _PIST_SOCKFNS_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <sys/types.h> // size_t
#include <pistache/em_socket_t.h>

/* ------------------------------------------------------------------------- */

// pist_sock_startup_check must be called before any winsock2 function. It can
// be called as many times as you like, it does nothing after the first time it
// is called, and it is threadsafe. All the pist_sock_xxx functions in this
// file call it themselves, so you don't need to call pist_sock_startup_check
// before calling pist_sock_socket for instance. However, if code outside of
// this file is calling winsock functions, that code must call
// pist_sock_startup_check, using the macro provided in winornix.h.
//
// Returns 0 on success, or -1 on failure with errno set.
int pist_sock_startup_check();

// Returns 0 for success. On fail, rets -1, and errno is set
int pist_sock_getsockname(em_socket_t em_sock,
                          struct sockaddr *addr, PST_SOCKLEN_T *addrlen);

// pist_sock_close rets 0 for success. On fail, ret -1, and errno is set
int pist_sock_close(em_socket_t em_sock);

// On success, returns number of bytes read (zero meaning the connection has
// gracefully closed). On failure, -1 is returned and errno is set
PST_SSIZE_T pist_sock_read(em_socket_t em_sock, void *buf, size_t count);

// On success, returns number of bytes written. On failure, -1 is returned and
// errno is set. Note that, even on success, bytes written may be fewer than
// count.
PST_SSIZE_T pist_sock_write(em_socket_t em_sock, const void *buf,size_t count);

// On success, returns em_socket_t. On failure, -1 is returned and errno is
// set.
em_socket_t pist_sock_socket(int domain, int type, int protocol);

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_bind(em_socket_t em_sock, const struct sockaddr *addr,
                   PST_SOCKLEN_T addrlen);

// On success returns an em_socket_t for the accepted socket. On failure, -1 is
// returned and errno is set.
em_socket_t pist_sock_accept(em_socket_t em_sock, struct sockaddr *addr,
                             PST_SOCKLEN_T *addrlen);

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_connect(em_socket_t em_sock, const struct sockaddr *addr,
                      PST_SOCKLEN_T addrlen);

// On success, returns 0. On failure, -1 is returned and errno is set.
int pist_sock_listen(em_socket_t em_sock, int backlog);

PST_SSIZE_T pist_sock_send(em_socket_t em_sock, const void *buf,
                           size_t len, int flags);

// On success, returns the number of bytes received. On error, -1 is
// returned and errno is set. Returns 0 if connection closed gracefully.
PST_SSIZE_T pist_sock_recv(em_socket_t em_sock, void * buf, size_t len,
                           int flags);

typedef struct PST_POLLFD
{
    em_socket_t   fd;
    short         events;     /* requested events */
    short         revents;    /* returned events */
} PST_POLLFD_T;

typedef unsigned long PST_NFDS_T;

// On success, returns a nonnegative value which is the number of elements in
// the pollfds whose revents fields have been set to a non‐ zero value
// (indicating an event or an error).  A return value of zero indicates that
// the system call timed out before any file descriptors became readable.
// On error, -1 is returned, and errno is set.
int pist_sock_poll(PST_POLLFD_T * fds, PST_NFDS_T nfds, int timeout);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_SOCKFNS_H_
