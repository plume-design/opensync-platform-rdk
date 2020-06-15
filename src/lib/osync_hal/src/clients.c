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

#include "osync_hal.h"
#include "util.h"
#include "log.h"
#include "const.h"


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_FETCH_CONNECTED_CLIENTS

// This is how mesh agent retrieves eth clients. It is done the same here.
#define  RDK_MESH_GET_ETH_CLIENTS_CMD "dmcli eRT getv Device.Hosts.Host. > /tmp/client_list.txt; /usr/ccsp/mesh/active_host_filter.sh /tmp/client_list.txt"

typedef enum
{
    CLIENT_MAC = 1,
    CLIENT_TYPE,
    CLIENT_HOSTNAME,
    // Indicate irrelevant columns
    CLIENT_IGNORE
} client_col_t;

osync_hal_return_t osync_hal_fetch_connected_clients(osync_hal_handle_client_fn handle_client_fn)
{
    osync_hal_clients_info_t osync_client;
    char *buf;
    char *line;
    const char *token;
    size_t col_i;
    const client_col_t col[] = {
        CLIENT_MAC,
        CLIENT_TYPE,
        CLIENT_IGNORE  // client hostname
    };

    buf = strexa("sh", "-c", RDK_MESH_GET_ETH_CLIENTS_CMD);
    if (buf == NULL)
    {
        LOGE("Fetching clients failed");
        return OSYNC_HAL_FAILURE;
    }

    while ((line = strsep(&buf, "\n")) != NULL)
    {
        memset(&osync_client, 0, sizeof(osync_hal_iface_type_t));
        col_i = 0;

        strchomp(line, "\n");

        // XX:XX:XX:XX:XX:XX|Ethernet|hostname
        while ((token = strsep(&line, "|")) != NULL)
        {
            const client_col_t curr_col = col[col_i];

            switch (curr_col)
            {
                case CLIENT_MAC:
                    STRSCPY(osync_client.mac_str, token);
                    break;
                case CLIENT_TYPE:
                    /* Skip Wifi client */
                    if (!strcmp(token, "Ethernet"))
                    {
                        osync_client.iface = OSYNC_HAL_IFACE_ETHERNET;
                    }
                    break;
                case CLIENT_IGNORE:
                    break;
                default:
                    LOGE("Clients: Encountered unknown osync_hal_client_col_t value: %d", curr_col);
                    continue;
            }

            col_i++;
        }

        if (col_i != ARRAY_LEN(col))
        {
            LOGE("Clients: Command entry has incorrect number of fields (%d columns found, expected %d): '%s'",
                    (int)col_i, ARRAY_LEN(col), line);
            continue;
        }

        if (handle_client_fn != NULL)
        {
            handle_client_fn(&osync_client);
        }
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_FETCH_CONNECTED_CLIENTS */
