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
 * wifihal_clients.c
 *
 * RDKB Platform - Wifi HAL - Clients
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/ether.h>
#include <ev.h>

#include "os.h"
#include "os_nif.h"
#include "evsched.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"
#include "ds_tree.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID       LOG_MODULE_ID_HAL

/*****************************************************************************/

typedef bool (*wifihal_clients_cb_t)(
                struct schema_Wifi_Associated_Clients *schema,
                char *ifname,
                bool status);

typedef struct
{
    char                mac[WIFIHAL_MAX_MACSTR];
    char                key_id[WIFIHAL_MAX_BUFFER];

    wifihal_ssid_t      *ssid;

    ds_tree_node_t      dst_node;
} wifihal_client_t;

static wifihal_clients_cb_t wifihal_clients_cb = NULL;
static ds_tree_t            wifihal_connected_clients;

/*****************************************************************************/

static bool
wifihal_clients_update(
        wifihal_ssid_t *ssid,
        const char *mac,
        const char *key_id,
        bool connected)
{
    struct schema_Wifi_Associated_Clients       cschema;

    if (!wifihal_clients_cb) {
        return false;
    }

    memset(&cschema, 0, sizeof(cschema));
    strncpy(cschema.mac, mac, sizeof(cschema.mac)-1);
    if (key_id) {
        strncpy(cschema.key_id, key_id, sizeof(cschema.key_id)-1);
    }
    cschema.key_id_exists = (strlen(key_id) > 0);
    if (connected) {
        strcpy(cschema.state, "active");
    } else {
        strcpy(cschema.state, "inactive");
    }

    wifihal_clients_cb(
            &cschema,
            target_unmap_ifname(ssid->ifname),
            connected);

    return true;
}


/*****************************************************************************/

void
wifihal_clients_connection(
        wifihal_ssid_t *ssid,
        char *mac,
        char *key_id)
{
    wifihal_client_t        *client;

    if (!ssid || !mac) {
        return;
    }

    client = ds_tree_find(&wifihal_connected_clients, mac);
    if (!client)
    {
        LOGI("%s: New client '%s' connected", ssid->ifname, mac);

        if (!(client = calloc(1, sizeof(*client)))) {
            LOGE("%s: Failed to allocate memory for new client", ssid->ifname);
            return;
        }

        strncpy(client->mac, mac, sizeof(client->mac)-1);
        client->ssid = ssid;
        ds_tree_insert(&wifihal_connected_clients, client, client->mac);
    }
    else if (client->ssid != ssid)
    {
        LOGI("%s: Client '%s' connection moving from %s",
             ssid->ifname, client->mac, client->ssid->ifname);
        wifihal_clients_update(client->ssid, client->mac, client->key_id, false);
        client->ssid = ssid;
    }
    else
    {
        LOGI("%s: Client '%s' re-connected", ssid->ifname, client->mac);
    }

    if (key_id)
    {
        strncpy(client->key_id, key_id, sizeof(client->key_id)-1);
    }
    else if (strlen(client->key_id) == 0)
    {
        strncpy(client->key_id, WIFIHAL_KEY_DEFAULT, sizeof(client->key_id)-1);
    }

    wifihal_clients_update(client->ssid, client->mac, client->key_id, true);

    return;
}

void
wifihal_clients_disconnection(wifihal_ssid_t *ssid, char *mac)
{
    wifihal_client_t        *client;

    if (!ssid || !mac) {
        return;
    }

    client = ds_tree_find(&wifihal_connected_clients, mac);
    if (client && client->ssid != ssid)
    {
        LOGI("%s: Client '%s' disconnect ignored (active on %s)",
             ssid->ifname, client->mac, client->ssid->ifname);
        return;
    }

    LOGI("%s: Client disconnected (%s)", ssid->ifname, mac);
    wifihal_clients_update(ssid, mac, client ? client->key_id : NULL, false);

    return;
}


/*****************************************************************************/

bool
wifihal_clients_init(void *clients_update_cb)
{
    if (wifihal_clients_cb) {
        return false;
    }

    ds_tree_init(&wifihal_connected_clients,
                 (ds_key_cmp_t *)strcmp,
                 wifihal_client_t,
                 dst_node);
    wifihal_clients_cb = clients_update_cb;

#ifdef WPA_CLIENTS
    if (!wifihal_clients_wpa_init())
#else
    if (!wifihal_clients_hal_init())
#endif
        return false;

    return true;
}

bool
wifihal_clients_cleanup(void)
{
    wifihal_client_t    *client;
    ds_tree_iter_t      iter;

    client = ds_tree_ifirst(&iter, &wifihal_connected_clients);
    while (client)
    {
        ds_tree_iremove(&iter);
        free(client);
        client = ds_tree_inext(&iter);
    }

#ifdef WPA_CLIENTS
    wifihal_clients_wpa_cleanup();
#else
    wifihal_clients_hal_cleanup();
#endif

    wifihal_clients_cb = NULL;

    return true;
}
