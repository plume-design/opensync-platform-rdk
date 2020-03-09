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

#define _GNU_SOURCE
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "os.h"
#include "log.h"
#include "util.h"
#include "const.h"
#include "osync_hal.h"

#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_DHCP_RESYNC_ALL
typedef enum
{
    DNSMASQ_LEASE_HWADDR = 1,
    DNSMASQ_LEASE_IPADDR,
    DNSMASQ_LEASE_HOST,
    DNSMASQ_LEASE_FINGER,
    // Indicate irrelevant columns
    DNSMASQ_LEASE_IGNORE
} dnsmasq_lease_col_t;
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_DHCP_RESYNC_ALL */


/**
 * Parse the the DHCP lease list from the dhcp.leases file and update the current list
 * Can be called from Mesh Agent.
 */
#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_DHCP_RESYNC_ALL
osync_hal_return_t osync_hal_dhcp_resync_all(osync_hal_dhcp_lease_fn dhcp_lease_fn)
{
    char buf[1024];
    osync_hal_dhcp_lease_t dlip;
    FILE *f = NULL;

    f = fopen(CONFIG_OSYNC_HAL_DHCP_LEASES_PATH, "rt");
    if (f == NULL)
    {
        LOGE("DHCP: Failed to open %s", CONFIG_OSYNC_HAL_DHCP_LEASES_PATH);
        return OSYNC_HAL_FAILURE;
    }

    while (fgets(buf, sizeof(buf), f) != NULL)
    {
        dnsmasq_lease_col_t col[] = {
            DNSMASQ_LEASE_IGNORE,    // Timestamp
            DNSMASQ_LEASE_HWADDR,    // MAC address
            DNSMASQ_LEASE_IPADDR,    // IP address
            DNSMASQ_LEASE_HOST,      // Hostname, can be '*'
            DNSMASQ_LEASE_FINGER,    // Fingerprint, can be '*'
            DNSMASQ_LEASE_IGNORE,    // CID, can be '*'
        };
        const char *token = NULL;
        char *line = strdupa(buf);
        size_t col_i = 0;

        // Extract the data
        memset(&dlip, 0, sizeof(dlip));

        // XXX: dnsmasq doesn't record the lease time in dhcp.lease.
        // Instead it records the future timestamp when the lease will expire.
        // This is somewhat unusable, so just use an arbitrary value:
        dlip.lease_time = 1;

        strchomp(line, "\n");
        while ((token = strsep(&line, " ")))
        {
            const dnsmasq_lease_col_t curr_col = col[col_i];

            switch(curr_col)
            {
                case DNSMASQ_LEASE_HWADDR:
                    STRSCPY(dlip.mac_str, token);
                    break;
                case DNSMASQ_LEASE_IPADDR:
                    STRSCPY(dlip.ip_str, token);
                    break;
                case DNSMASQ_LEASE_HOST:
                    STRSCPY(dlip.hostname, token);
                    break;
                case DNSMASQ_LEASE_FINGER:
                    STRSCPY(dlip.fingerprint, token);
                    break;
                case DNSMASQ_LEASE_IGNORE:
                    // Skip this field
                    break;
                default:
                    LOGE("DHCP: Encountered unknown osync_hal_dnsmasq_lease_col_t value: %d", curr_col);
                    continue;
            }

            col_i++;
        }

        if (col_i != ARRAY_LEN(col))
        {
            LOGE("DHCP: dnsmasq.leases entry has incorrect number of fields (%d columns found, expected %d): '%s'",
                    (int)col_i, ARRAY_LEN(col), buf);
            continue;
        }

        if (dhcp_lease_fn != NULL)
        {
            dhcp_lease_fn(&dlip);
        }
    }

    if (f != NULL)
    {
        fclose(f);
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_DHCP_RESYNC_ALL */
