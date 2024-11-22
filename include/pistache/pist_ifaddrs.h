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

// #include <Ws2def.h> // for sockaddr

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

// Defines for ifa_flags
// As per /usr/include/net/if.h in Ubuntu:
#define PST_IFF_UP 0x1               /* Interface is up.  */
#define PST_IFF_BROADCAST 0x2        /* Broadcast address valid.  */
#define PST_IFF_DEBUG 0x4            /* Turn on debugging.  */
#define PST_IFF_LOOPBACK 0x8         /* Is a loopback net.  */
#define PST_IFF_POINTOPOINT 0x10     /* Interface is point-to-point link.  */
#define PST_IFF_NOTRAILERS 0x20      /* Avoid use of trailers.  */
#define PST_IFF_RUNNING 0x40         /* Resources allocated.  */
#define PST_IFF_NOARP 0x80           /* No address resolution protocol.  */
#define PST_IFF_PROMISC 0x100        /* Receive all packets.  */

// The following flags are not supported in Linux, again as per if.h:
#define PST_IFF_ALLMULTI 0x200       /* Receive all multicast packets.  */
#define PST_IFF_MASTER 0x400         /* Master of a load balancer.  */
#define PST_IFF_SLAVE 0x800          /* Slave of a load balancer.  */
#define PST_IFF_MULTICAST 0x1000     /* Supports multicast.  */
#define PST_IFF_PORTSEL 0x2000       /* Can set media type.  */
#define PST_IFF_AUTOMEDIA 0x4000     /* Auto media select active.  */
#define PST_IFF_DYNAMIC 0x8000       /* Dialup device with changing addresses*/


extern "C" int PST_GETIFADDRS(struct PST_IFADDRS **ifap);

extern "C" void PST_FREEIFADDRS(struct PST_IFADDRS *ifa);

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */
#endif // of ifndef _PIST_IFADDRS_H_
