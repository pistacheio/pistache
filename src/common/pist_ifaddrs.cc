/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines "struct pist_ifaddrs", and the functions pist_getifaddrs and
// pist_freeifaddrs for Windows
//

/* ------------------------------------------------------------------------- */

#include <pistache/winornix.h>

#ifdef _IS_WINDOWS

#include <vector>

#include <pistache/pist_syslog.h>
#include <pistache/pist_ifaddrs.h>
#include <pistache/ps_strl.h>

#include <winsock2.h>
#include <iphlpapi.h> // Required for netioapi.h
#include <netioapi.h> // for ConvertLengthToIpv4Mask
#include <iptypes.h> // For IP_ADAPTER_ADDRESSES

/* ------------------------------------------------------------------------- */

extern "C" int PST_GETIFADDRS(struct PST_IFADDRS **ifap)
{
    if (!ifap)
    {
        PS_LOG_DEBUG("No ifap");
        errno = EINVAL;
        return(-1);
    }
    *ifap = nullptr;

    ULONG buff_len = sizeof(IP_ADAPTER_ADDRESSES);
    std::vector<unsigned char> buff_try1_vec(buff_len+16);
    PIP_ADAPTER_ADDRESSES fst_adap_addr =
        (PIP_ADAPTER_ADDRESSES)(buff_try1_vec.data());

    // First try for GetAdaptersAddresses, allowing space for just one address
    // Probably that's not enough, but it will tell us how much buffer space we
    // need (buff_len is written to, telling us)
    ULONG gaa_res1 = GetAdaptersAddresses(
        AF_UNSPEC, // IP4 and IP6
        GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER,
        nullptr, // reserved
        fst_adap_addr,
        &buff_len);
    ULONG gaa_res = gaa_res1;

    std::vector<unsigned char> buff_try2_vec(buff_len+16);

    if (gaa_res == ERROR_BUFFER_OVERFLOW)
    {
        fst_adap_addr = (PIP_ADAPTER_ADDRESSES)(buff_try2_vec.data());

        gaa_res = GetAdaptersAddresses(
            AF_UNSPEC, // IP4 and IP6
            GAA_FLAG_INCLUDE_ALL_INTERFACES |
                GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER,
            nullptr, // reserved
            fst_adap_addr,
            &buff_len);
    }

    if (gaa_res != ERROR_SUCCESS)
    {
        PS_LOG_INFO_ARGS("GetAdaptersAddresses failed, gaa_res %d", gaa_res);

        int res = -1;

        switch(gaa_res)
        {
        case ERROR_BUFFER_OVERFLOW:
            errno = EOVERFLOW;
            break;

        case ERROR_NOT_ENOUGH_MEMORY:
            errno = ENOMEM;
            break;

        case ERROR_INVALID_PARAMETER:
            errno = EINVAL;
            break;

        case ERROR_ADDRESS_NOT_ASSOCIATED:
        case ERROR_NO_DATA:
            res = 0;
            PS_LOG_DEBUG("No addresses found, ret success with empty list");
            break;

        default:
            PS_LOG_DEBUG("Unexpected error for GetAdaptersAddresses");
            errno = EINVAL;
            break;
        }

        return(res);
    }

    if (buff_len == 0)
    {
        PS_LOG_DEBUG("No addresses found, ret success with empty list");
        return(0);
    }

    unsigned int num_adap_addr = 0;
    for (PIP_ADAPTER_ADDRESSES adap_addr = fst_adap_addr; adap_addr;
         adap_addr = adap_addr->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS unicast_addr =
                                                adap_addr->FirstUnicastAddress;
        while(unicast_addr)
        {
            ++num_adap_addr;
            unicast_addr = unicast_addr->Next;
        };
    }

    if (!num_adap_addr)
    {
        PS_LOG_DEBUG(
            "No unicast addresses found, ret success with empty list");
        return(0);
    }


    struct PST_IFADDRS * pst_ifaddrs = new PST_IFADDRS[num_adap_addr+1];
    if (!pst_ifaddrs)
    {
        PS_LOG_WARNING("new failed");
        errno = ENOMEM;
        return(-1);
    }

    #define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
    #define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

    unsigned int i = 0;
    for (PIP_ADAPTER_ADDRESSES adap_addr = fst_adap_addr; adap_addr;
         adap_addr = adap_addr->Next)
    {
        PIP_ADAPTER_UNICAST_ADDRESS unicast_addr =
            adap_addr->FirstUnicastAddress;
        if (!unicast_addr)
            continue;

        /*
         SIOCGIFFLAGS
              Get or set the active flag word of the device.  ifr_flags
              contains a bit mask of the following values:
                                      Device flags
              IFF_UP            Interface is running.
              IFF_BROADCAST     Valid broadcast address set.
              IFF_DEBUG         Internal debugging flag.
              IFF_LOOPBACK      Interface is a loopback interface.
              IFF_POINTOPOINT   Interface is a point-to-point link.
              IFF_RUNNING       Resources allocated.
              IFF_NOARP         No arp protocol, L2 destination address not
                                set.
              IFF_PROMISC       Interface is in promiscuous mode.
              IFF_NOTRAILERS    Avoid use of trailers.
              IFF_ALLMULTI      Receive all multicast packets.
              IFF_MASTER        Master of a load balancing bundle.
              IFF_SLAVE         Slave of a load balancing bundle.
              IFF_MULTICAST     Supports multicast
              IFF_PORTSEL       Is able to select media type via ifmap.
              IFF_AUTOMEDIA     Auto media selection active.
              IFF_DYNAMIC       The addresses are lost when the interface
                                goes down.
              IFF_LOWER_UP      Driver signals L1 up (since Linux 2.6.17)
              IFF_DORMANT       Driver signals dormant (since Linux 2.6.17)
              IFF_ECHO          Echo sent packets (since Linux 2.6.25)
        */
        unsigned int adap_flags = 0;
        adap_flags |= ((adap_addr->OperStatus == IfOperStatusUp) ?
                       PST_IFF_UP : 0);
        adap_flags |= ((adap_addr->FirstMulticastAddress) ?
                       PST_IFF_BROADCAST : 0);
        adap_flags |= ((adap_addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) ?
                       PST_IFF_LOOPBACK:0);

        // "Point-to-point link" usually maeans two machines linked with a
        // single wire and no other devices on the wire. GetAdaptersAddresses
        // doesn't seem to distinguish that from unicast

        adap_flags |= ((adap_addr->OperStatus == IfOperStatusUp) ?
                  PST_IFF_RUNNING : 0);

        adap_flags |= ((adap_addr->Flags & IP_ADAPTER_NO_MULTICAST) ?
                  0 : PST_IFF_MULTICAST);

        adap_flags |=
            ((adap_addr->ConnectionType == NET_IF_CONNECTION_DEDICATED) ?
             PST_IFF_AUTOMEDIA : 0);

        // IFF_NOARP, IFF_PROMISC, IFF_NOTRAILERS, IFF_ALLMULTI, IFF_MASTER,
        // IFF_SLAVE, IFF_PORTSEL, IFF_DYNAMIC, IFF_LOWER_UP, IFF_DORMANT and
        // IFF_ECHO don't seem supported in GetAdaptersAddresses

        // We already checked that first unicast_addr is non null
        do {
            const LPSOCKADDR win_sock_addr = unicast_addr->Address.lpSockaddr;
            int win_sock_addr_len = unicast_addr->Address.iSockaddrLength;
            if ((!win_sock_addr) || (!win_sock_addr_len))
                continue;

            PST_IFADDRS & this_ifaddrs(pst_ifaddrs[i]);
            memset(&this_ifaddrs, 0, sizeof(this_ifaddrs));

            if (adap_addr->AdapterName)
            {
                size_t name_len = strlen(adap_addr->AdapterName);
                this_ifaddrs.ifa_name = reinterpret_cast<char *>(MALLOC(name_len+1));
                if (!this_ifaddrs.ifa_name)
                {
                    PS_LOG_WARNING("Name MALLOC failed");
                    delete[] pst_ifaddrs;
                    errno = ENOMEM;
                    return(-1);
                }

                PS_STRLCPY(this_ifaddrs.ifa_name,
                           adap_addr->AdapterName, name_len+1);
            }

            this_ifaddrs.ifa_flags = adap_flags;

            LPSOCKADDR sock_addr = reinterpret_cast<LPSOCKADDR>(MALLOC(sizeof(SOCKADDR)));
            if (!sock_addr)
            {
                PS_LOG_WARNING("MALLOC failed");
                delete[] pst_ifaddrs;
                errno = ENOMEM;
                return(-1);
            }

            *sock_addr = *win_sock_addr;
            this_ifaddrs.ifa_addr = sock_addr;

            if (unicast_addr->OnLinkPrefixLength)
            {
                if (win_sock_addr->sa_family == AF_INET)
                { // IPv4
                    ULONG mask = 0;
                    if ((ConvertLengthToIpv4Mask(
                             unicast_addr->OnLinkPrefixLength,
                             &mask) == NO_ERROR) && (mask != 0))
                    {
                        LPSOCKADDR mask_sock_addr = (LPSOCKADDR)
                            MALLOC(sizeof(SOCKADDR));
                        if (!mask_sock_addr)
                        {
                            PS_LOG_WARNING("MALLOC failed");
                            delete[] pst_ifaddrs;
                            errno = ENOMEM;
                            return(-1);
                        }
                        memset(mask_sock_addr, 0, sizeof(*mask_sock_addr));
                        mask_sock_addr->sa_family =
                            win_sock_addr->sa_family;
                        ((sockaddr_in *)mask_sock_addr)->
                            sin_addr.S_un.S_addr = mask;

                        this_ifaddrs.ifa_netmask = mask_sock_addr;
                    }
                }
            }

            ++i;
            if (i >= num_adap_addr)
                break;

            this_ifaddrs.ifa_next = &(pst_ifaddrs[i]);

            unicast_addr = unicast_addr->Next;
        } while(unicast_addr);
    }

    *ifap = pst_ifaddrs;
    return(0);
}


/* ------------------------------------------------------------------------- */

extern "C" void PST_FREEIFADDRS(struct PST_IFADDRS *ifa)
{
    if (!ifa)
    {
        PS_LOG_DEBUG("ifa is NULL");
        return;
    }

    for (PST_IFADDRS * this_ifaddrs = ifa; this_ifaddrs;
         this_ifaddrs = this_ifaddrs->ifa_next)
    {
        if (this_ifaddrs->ifa_name)
            FREE(this_ifaddrs->ifa_name);

        if (this_ifaddrs->ifa_addr)
            FREE(this_ifaddrs->ifa_addr);

        if (this_ifaddrs->ifa_netmask)
            FREE(this_ifaddrs->ifa_netmask);
    }

    delete ifa;
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS
