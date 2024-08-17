/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Defines "struct pist_ifaddrs", and the functions pist_getifaddrs and
// pist_freeifaddrs for Windows
//
#include <pistache/pist_ifaddrs.h>

/* ------------------------------------------------------------------------- */

#ifdef _IS_WINDOWS

/* ------------------------------------------------------------------------- */

// !!!!!!!!
// See https://stackoverflow.com/questions/41139561/find-ip-address-of-the-machine-in-c

extern "C" int PST_GETIFADDRS(struct PST_IFADDRS **ifap)
{
    if (!ifap)
    {
        PS_LOG_DEBUG("No ifap");
        errno = EINVAL;
        return(-1);
    }
    *ifap = NULL;

    ULONG buff_len = sizeof(IP_ADAPTER_ADDRESSES);
    u_char buff_try1[buff_len+16];
    PIP_ADAPTER_ADDRESSES fst_adap_addr = (PIP_ADAPTER_ADDRESSES)(&buff_try1[0]);

    // First try for GetAdaptersAddresses, allowing space for just one address
    // Presuming that's not enough, it will tell us how much buffer space we need
    ULONG gaa_res1 = GetAdaptersAddresses(
        AF_UNSPEC, // IP4 and IP6
        GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER,
        NULL, // reserved
        fst_adap_addr,
        &buff_len);
    ULONG gaa_res = gaa_res1;

    u_char buff_try2[buff_len+16];

    if (gaa_res == ERROR_BUFFER_OVERFLOW)
    {
        fst_adap_addr = (PIP_ADAPTER_ADDRESSES)(&buff_try2[0]);
        
        gaa_res = GetAdaptersAddresses(
            AF_UNSPEC, // IP4 and IP6
            GAA_FLAG_INCLUDE_ALL_INTERFACES | GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER,
            NULL, // reserved
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
        bool is_loopback = (adap_addr->IfType != IF_TYPE_SOFTWARE_LOOPBACK);
        PIP_ADAPTER_UNICAST_ADDRESS unicast_addr = adap_addr->FirstUnicastAddress;
        if ((!unicast_addr) && (!is_loopback))
            continue;

        if (!unicast_addr)
        {
            num_adap_addr++;
            continue;
        }

        do {
            num_adap_addr++;
            unicast_addr = unicast_addr->Next;
        } while(unicast_addr);
    }

    if (!num_adap_addr)
    {
        PS_LOG_DEBUG("No unicast addresses found, ret success with empty list");
        return(0);
    }


    struct PST_IFADDRS * pst_ifaddrs = new PST_IFADDRS[num_adap_addr+1];
    if (!pst_ifaddrs)
    {
        PS_LOG_DEBUG("new failed");
        errno = ENOMEM;
        return(-1);
    }

    #define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
    #define FREE(x) HeapFree(GetProcessHeap(), 0, (x))
    
    unsigned int i = 0;
    for (PIP_ADAPTER_ADDRESSES adap_addr = fst_adap_addr; adap_addr;
         adap_addr = adap_addr->Next)
    {
        bool is_loopback = (adap_addr->IfType != IF_TYPE_SOFTWARE_LOOPBACK);
        PIP_ADAPTER_UNICAST_ADDRESS unicast_addr = adap_addr->FirstUnicastAddress;
        if ((!unicast_addr) && (!is_loopback))
            continue;

        

        PST_IFADDRS base_ifaddrs;
        memset(&base_ifaddrs, 0, sizeof(base_ifaddrs));

        if (adap_addr->AdapterName)
        {
            size_t name_len = strlen(adap_addr->AdapterName);
            base_ifaddrs.ifa_name = MALLOC(name_len+1);
            if (!base_ifaddrs.ifa_name)
            {
                PS_LOG_DEBUG("Name MALLOC failed");
            }
            else
            {
                strncpy(base_ifaddrs.ifa_name, adap_addr->AdapterName, name_len);
                base_ifaddrs.ifa_name[name_len] = 0;
            }
        }

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
        unsigned int flags = 0;
        flags |= ((adap_addr->OperStatus == IfOperStatusUp) ? PST_IFF_UP : 0);
        flags |= ((adap_addr->FirstMulticastAddress) ? PST_IFF_BROADCAST : 0);
        flags |=
            ((adap_addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) ? PST_IFF_LOOPBACK:0);
        
        // "Point-to-point link" usually maeans two machines linked with a
        // single wire andnno other devices on the wire. GetAdaptersAddresses
        // doesn't seem to distinguish that from unicast
        
        flags |= ((adap_addr->OperStatus == IfOperStatusUp) ? PST_IFF_RUNNING : 0);

        flags |=
            ((adap_addr->Flags & IP_ADAPTER_NO_MULTICAST) ? 0 : PST_IFF_MULTICAST);
        
        flags |= ((adap_addr->ConnectionType == NET_IF_CONNECTION_DEDICATED) ?
                  PST_IFF_AUTOMEDIA : 0);
        
        // IFF_NOARP, IFF_PROMISC, IFF_NOTRAILERS, IFF_ALLMULTI, IFF_MASTER,
        // IFF_SLAVE, IFF_PORTSEL, IFF_DYNAMIC, IFF_LOWER_UP, IFF_DORMANT and
        // IFF_ECHO don't seem supported in GetAdaptersAddresses

        base_ifaddrs.ifa_flags = flags;

        
        if (!unicast_addr)
        {
            PST_IFADDRS & this_ifaddrs(pst_ifaddrs[i]);
            memset(&this_ifaddrs, 0, sizeof(this_ifaddrs));
            
            this_ifaddrs = base_ifaddrs;

            // It's a loopback interface but not specifying a unicast address
            // !!!!!!!! - fill in based on 127.0.0.1

            i++;
            if (i >= num_adap_addr)
                break;

            base_ifaddrs.ifa_next = &(pst_ifaddrs[i]);
        }
        else
        {
            do {
                
                PST_IFADDRS & this_ifaddrs(pst_ifaddrs[i]);
                memset(&this_ifaddrs, 0, sizeof(this_ifaddrs));

                this_ifaddrs = base_ifaddrs;

                // !!!!!!!!
                // Fill in ifa_addr based on unicast_addr
                

                i++;
                if (i >= num_adap_addr)
                    break;

                base_ifaddrs.ifa_next = &(pst_ifaddrs[i]);

                unicast_addr = unicast_addr->Next;
            } while(unicast_addr);
        }
    }

    
    *ifap = fst_adap_addr;
    return(0);
}


/* ------------------------------------------------------------------------- */

extern "C" void PST_FREEIFADDRS(struct PST_IFADDRS *ifa)
{
}

/* ------------------------------------------------------------------------- */

#endif // of ifdef _IS_WINDOWS

