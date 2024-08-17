/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines "struct pist_ifaddrs", and the functions pist_getifaddrs and
// pist_freeifaddrs for Windows
//
// #include <pistache/pist_ifaddrs.h>

#ifndef _PIST_IFADDRS_H_
#define _PIST_IFADDRS_H_

#include <pistache/winornix.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

#include <Ws2def.h> // for sockaddr

/* ------------------------------------------------------------------------- */

// Per Linux "man getifaddrs"
struct PST_IFADDRS {
               struct PST_IFADDRS *ifa_next; /* Next item in list */
               char            *ifa_name;    /* Name of interface */
               unsigned int     ifa_flags;   /* Flags from SIOCGIFFLAGS */
               struct sockaddr *ifa_addr;    /* Address of interface */
               struct sockaddr *ifa_netmask; /* Netmask of interface */
               union {
                   struct sockaddr *ifu_broadaddr;
                                    /* Broadcast address of interface */
                   struct sockaddr *ifu_dstaddr;
                                    /* Point-to-point destination address */
               } ifa_ifu;
           #define              pist_ifa_broadaddr ifa_ifu.ifu_broadaddr
           #define              pist_ifa_dstaddr   ifa_ifu.ifu_dstaddr
               void            *ifa_data;    /* Address-specific data */
           };

extern "C" int PST_GETIFADDRS(struct PST_IFADDRS **ifap);

extern "C" void PST_FREEIFADDRS(struct PST_IFADDRS *ifa);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_IFADDRS_H_
