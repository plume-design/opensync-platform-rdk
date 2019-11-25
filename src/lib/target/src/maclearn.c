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
 * maclearn.c
 *
 * RDKB Platform - Wifi HAL - Mac Learning Table Handling (Wired clients)
 */

#include <stdbool.h>

#include "log.h"
#include "target.h"
#include "ds_tree.h"

/*****************************************************************************/

#define MODULE_ID                       LOG_MODULE_ID_HAL
#define MACLEARN_BRIDGE                 "br-home"

/*****************************************************************************/

typedef bool (*maclearn_cb_t)(
        struct schema_OVS_MAC_Learning *omac,
        bool active);

static maclearn_cb_t    maclearn_cb = NULL;

static c_item_t map_ifname[] =
{
    C_ITEM_STR(MACLEARN_TYPE_ETH,           "eth0"),
    C_ITEM_STR(MACLEARN_TYPE_MOCA,          "moca0"),
};

/*****************************************************************************/

bool maclearn_update(maclearn_type_t type, const char *mac, bool connected)
{
    struct schema_OVS_MAC_Learning              omac;
    c_item_t                                    *citem;

    if (maclearn_cb == NULL)
    {
        return false;
    }

    citem = c_get_item_by_key(map_ifname, type);
    if (citem == NULL)
    {
        LOGW("Ignoring MAC Learning update for '%s', unknown type %d", mac, type);
        return false;
    }

    memset(&omac, 0, sizeof(omac));
    STRSCPY(omac.hwaddr, mac);
    STRSCPY(omac.brname, MACLEARN_BRIDGE);
    STRSCPY(omac.ifname, c_get_str_by_key(map_ifname, type));

    LOGD("Mac Learning: Bridge \"%s\", Interface \"%s\", Client \"%s\" now %sconnected",
         omac.brname, omac.ifname, omac.hwaddr, connected ? "" : "dis");
    maclearn_cb(&omac, connected);

    return true;
}

bool maclearn_init(void *maclearn_cb)
{
    if (maclearn_cb != NULL)
    {
        return false;
    }

    maclearn_cb = maclearn_cb;
    LOGN("OVS Mac Learning callback registered");

    return true;
}
