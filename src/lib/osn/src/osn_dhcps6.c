/*
Copyright (c) 2017, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * ===========================================================================
 *  DNSMASQ provides the implementation for the following OpenSync APIs:
 *      - DHCPv6 server
 *      - Router Advertisement
 * ===========================================================================
 */

#include <errno.h>

#include "osn_types.h"
#include "osn_dhcpv6.h"

#include "ds.h"
#include "ds_tree.h"
#include "ds_dlist.h"
#include "const.h"
#include "log.h"
#include "util.h"

static int dummy_osn_dhcpv6_server;

/*
 * ===========================================================================
 *  DHCPv6 Server Public API
 * ===========================================================================
 */

/*
 * Create new DHCPv6 server instance
 */
osn_dhcpv6_server_t* osn_dhcpv6_server_new(const char *ifname)
{
    return (osn_dhcpv6_server_t *)&dummy_osn_dhcpv6_server;
}

/*
 * Delete a DHCPv6 Server instance
 */
bool osn_dhcpv6_server_del(osn_dhcpv6_server_t *self)
{
    return true;
}

bool osn_dhcpv6_server_apply(osn_dhcpv6_server_t *self)
{
    return true;
}

bool osn_dhcpv6_server_set(
        osn_dhcpv6_server_t *self,
        bool prefix_delegation)
{
    return true;
}

/*
 * Add the prefix to the pool that the DHCPv6 server will be serving
 */
bool osn_dhcpv6_server_prefix_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    return true;
}

/*
 * Remove the prefix from the DHCPv6 server list
 */
bool osn_dhcpv6_server_prefix_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    return true;
}

/*
 * Set DHCPv6 options
 */
bool osn_dhcpv6_server_option_send(
        osn_dhcpv6_server_t *self,
        int tag,
        const char *data)
{
    return true;
}

/*
 * Add static lease
 */
bool osn_dhcpv6_server_lease_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    return true;
}

/*
 * Remove static lease
 */
bool osn_dhcpv6_server_lease_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    return true;
}

/*
 * Set the notification/status update
 */
bool osn_dhcpv6_server_status_notify(
        osn_dhcpv6_server_t *self,
        osn_dhcpv6_server_status_fn_t *fn)
{
    return true;
}

/* Set/get private data */
void osn_dhcpv6_server_data_set(
        osn_dhcpv6_server_t *self,
        void *data)
{
    return;
}

void* osn_dhcpv6_server_data_get(osn_dhcpv6_server_t *self)
{
    return NULL;
}

static int dummy_ipv6_radv_data;

osn_ip6_radv_t* osn_ip6_radv_new(const char *ifname)
{
    return (osn_ip6_radv_t *)&dummy_ipv6_radv_data;
}

bool osn_ip6_radv_del(osn_ip6_radv_t *self)
{
    return true;
}

bool osn_ip6_radv_apply(osn_ip6_radv_t *self)
{
    return true;
}

bool osn_ip6_radv_set(
        osn_ip6_radv_t *self,
        const struct osn_ip6_radv_options *opts)
{
    return true;
}

bool osn_ip6_radv_add_prefix(
        osn_ip6_radv_t *self,
        const osn_ip6_addr_t *prefix,
        bool autonomous,
        bool onlink)
{
    return true;
}

bool osn_ip6_radv_add_rdnss(
        osn_ip6_radv_t *self,
        const osn_ip6_addr_t *rdnss)
{
    return true;
}

bool osn_ip6_radv_add_dnssl(
        osn_ip6_radv_t *self,
        char *dnssl)
{
    return true;
}

const char* dnsmasq6_server_option6_encode(
        int tag,
        const char *value)
{
    return value;
}

