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

#include <stdio.h>

#include "os.h"
#include "log.h"
#include "ds_tree.h"

#include "target.h"
#include "target_internal.h"

#define MODULE_ID LOG_MODULE_ID_OSA

#define HAL_CB_QUEUE_MAX    20

typedef struct
{
    char                mac[WIFIHAL_MAX_MACSTR];
    char                key_id[WIFIHAL_MAX_BUFFER];

    INT                 apIndex;

    ds_tree_node_t      dst_node;
} client_t;

static ds_tree_t            connected_clients;

typedef struct
{
    INT                     ssid_index;
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_associated_dev4_t  sta;
#else
    wifi_associated_dev_t   sta;
#endif

    ds_dlist_node_t         node;
} hal_cb_entry_t;

static struct ev_loop      *hal_cb_loop = NULL;
static pthread_mutex_t      hal_cb_lock;
static ev_async             hal_cb_async;
static ds_dlist_t           hal_cb_queue;
static int                  hal_cb_queue_len = 0;

static struct target_radio_ops g_rops;

#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
static INT clients_hal_assocdev_cb(INT ssid_index, wifi_associated_dev4_t *sta)
#else
static INT clients_hal_assocdev_cb(INT ssid_index, wifi_associated_dev_t *sta)
#endif
{
    hal_cb_entry_t      *cbe;
    INT                 ret = RETURN_ERR;

    pthread_mutex_lock(&hal_cb_lock);

    if (hal_cb_queue_len == HAL_CB_QUEUE_MAX)
    {
        LOGW("clients_hal_assocdev_cb: Queue is full! Ignoring event...");
        goto exit;
    }

    if ((cbe = calloc(1, sizeof(*cbe))) == NULL)
    {
        LOGE("clients_hal_assocdev_cb: Failed to allocate memory!");
        goto exit;
    }
    cbe->ssid_index = ssid_index;
    memcpy(&cbe->sta, sta, sizeof(cbe->sta));

    ds_dlist_insert_tail(&hal_cb_queue, cbe);
    hal_cb_queue_len++;
    ret = RETURN_OK;

exit:
    pthread_mutex_unlock(&hal_cb_lock);
    if (ret == RETURN_OK && hal_cb_loop)
    {
        if (!ev_async_pending(&hal_cb_async))
        {
            ev_async_send(hal_cb_loop, &hal_cb_async);
        }
    }
    return ret;
}

static INT clients_hal_dissocdev_cb(INT ssid_index, char *mac, INT event_type)
{
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_associated_dev4_t sta;
#else
    wifi_associated_dev_t sta;
#endif

    memset(&sta, 0, sizeof(sta));

    sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &sta.cli_MACAddress[0], &sta.cli_MACAddress[1], &sta.cli_MACAddress[2],
           &sta.cli_MACAddress[3], &sta.cli_MACAddress[4], &sta.cli_MACAddress[5]);
    sta.cli_Active = false;

    return clients_hal_assocdev_cb(ssid_index, &sta);
}

static void clients_hal_async_cb(EV_P_ ev_async *w, int revents)
{
    ds_dlist_iter_t     qiter;
    hal_cb_entry_t      *cbe;
    os_macaddr_t        macaddr;
    char                mac[20];
    char                ifname[256];

    pthread_mutex_lock(&hal_cb_lock);

    cbe = ds_dlist_ifirst(&qiter, &hal_cb_queue);
    while (cbe)
    {
        ds_dlist_iremove(&qiter);
        hal_cb_queue_len--;

        memcpy(&macaddr, cbe->sta.cli_MACAddress, sizeof(macaddr));
        snprintf(mac, sizeof(mac), PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));

        memset(ifname, 0, sizeof(ifname));
        if (wifi_getApName(cbe->ssid_index, ifname) != RETURN_OK)
        {
            LOGE("%s: cannot get AP name for index %d", __func__, cbe->ssid_index);
            free(cbe);
            cbe = ds_dlist_inext(&qiter);
            continue;
        }

        // Filter SSID's that we don't have mappings for
        if (!target_unmap_ifname_exists(ifname))
        {
            free(cbe);
            cbe = ds_dlist_inext(&qiter);
            LOGD("Skipping client '%s' for '%s' (iface not mapped)", mac, ifname);
            continue;
        }

        if (cbe->sta.cli_Active)
        {
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
            clients_connection(cbe->ssid_index, mac, cbe->sta.cli_MultiPskKeyID);
#else
            clients_connection(cbe->ssid_index, mac, NULL);
#endif
        }
        else
        {
            clients_disconnection(cbe->ssid_index, mac);
        }

        free(cbe);
        cbe = ds_dlist_inext(&qiter);
    }

    pthread_mutex_unlock(&hal_cb_lock);
    return;
}

bool clients_hal_fetch_existing(unsigned int apIndex)
{
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_associated_dev4_t  *associated_dev = NULL;
#else
    wifi_associated_dev3_t  *associated_dev = NULL;
#endif
    os_macaddr_t             macaddr;
    UINT                     num_devices = 0;
    ULONG                    i;
    char                     mac[20];
    INT                      ret;
    char                     ifname[256];


    memset(ifname, 0, sizeof(ifname));
    ret = wifi_getApName(apIndex, ifname);
    if (ret != RETURN_OK)
    {
        LOGE("Cannot get Ap Name for index %d", apIndex);
        return false;
    }

#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    ret = wifi_getApAssociatedDeviceDiagnosticResult4(apIndex, &associated_dev, &num_devices);
#else
    ret = wifi_getApAssociatedDeviceDiagnosticResult3(apIndex, &associated_dev, &num_devices);
#endif

    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to fetch associated devices", ifname);
        return false;
    }
    LOGD("%s: Found %u existing associated clients", ifname, num_devices);

    for (i = 0; i < num_devices; ++i)
    {
        memcpy(&macaddr, associated_dev[i].cli_MACAddress, sizeof(macaddr));
        snprintf(mac, sizeof(mac), PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));

        // Report connection
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
        clients_connection(apIndex, mac, associated_dev[i].cli_MultiPskKeyID);
#else
        clients_connection(apIndex, mac, NULL);
#endif
    }
    free(associated_dev);

    return true;
}

bool clients_hal_init(const struct target_radio_ops *rops)
{
    g_rops = *rops;

    // See if we've been called already
    if (hal_cb_loop != NULL)
    {
        // Already was initialized, just [re]start async watcher
        ev_async_start(hal_cb_loop, &hal_cb_async);
        return true;
    }

    ds_tree_init(&connected_clients,
            (ds_key_cmp_t *)strcmp,
            client_t,
            dst_node);

    // Save running evloop
    if (wifihal_evloop == NULL) {
        LOGE("clients_hal_init: Called before wifihal_evloop is initialized!");
        return false;
    }
    hal_cb_loop = wifihal_evloop;

    // Init CB Queue
    ds_dlist_init(&hal_cb_queue, hal_cb_entry_t, node);
    hal_cb_queue_len = 0;

    // Init mutex lock for queue
    pthread_mutex_init(&hal_cb_lock, NULL);

    // Init async watcher
    ev_async_init(&hal_cb_async, clients_hal_async_cb);
    ev_async_start(hal_cb_loop, &hal_cb_async);

    // Register callbacks (NOTE: calls callback from created pthread)
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_newApAssociatedDevice_callback_register2(clients_hal_assocdev_cb);
#else
    wifi_newApAssociatedDevice_callback_register(clients_hal_assocdev_cb);
#endif

    wifi_apDisassociatedDevice_callback_register(clients_hal_dissocdev_cb);

    return true;
}

static bool clients_update(
        char *ifname,
        const char *mac,
        const char *key_id,
        bool connected)
{
    struct schema_Wifi_Associated_Clients       cschema;

    memset(&cschema, 0, sizeof(cschema));
    cschema._partial_update = true;

    STRSCPY(cschema.mac, mac);
    if (key_id != NULL)
    {
        STRSCPY(cschema.key_id, key_id);
    }

    cschema.key_id_exists = key_id ? (strlen(key_id) > 0) : false;
    if (connected == true)
    {
        STRSCPY(cschema.state, "active");
    } else
    {
        STRSCPY(cschema.state, "inactive");
    }

    g_rops.op_client(&cschema, target_unmap_ifname(ifname), connected);

    return true;
}


/*****************************************************************************/

void clients_connection(
        INT apIndex,
        char *mac,
        char *key_id)
{
    client_t *client;
    char     ifname[256];
    char     ifname_old[256];

    memset(ifname, 0, sizeof(ifname));
    memset(ifname_old, 0, sizeof(ifname_old));
    if (mac == NULL) {
        return;
    }

    if (wifi_getApName(apIndex, ifname) != RETURN_OK)
    {
        LOGE("Cannot get apName for index %d\n", apIndex);
        return;
    }

    client = ds_tree_find(&connected_clients, mac);
    if (client == NULL)
    {
        LOGI("%s: New client '%s' connected", ifname, mac);

        if (!(client = calloc(1, sizeof(*client)))) {
            LOGE("%s: Failed to allocate memory for new client", ifname);
            return;
        }

        STRSCPY(client->mac, mac);
        client->apIndex = apIndex;
        ds_tree_insert(&connected_clients, client, client->mac);
    }
    else if (client->apIndex != apIndex)
    {
        if (wifi_getApName(client->apIndex, ifname_old) != RETURN_OK)
        {
            LOGE("Cannot get apName for index %d\n", client->apIndex);
            return;
        }

        LOGI("%s: Client '%s' connection moving from %s",
             ifname, client->mac, ifname_old);
        clients_update(ifname_old, client->mac, client->key_id, false);
        client->apIndex = apIndex;
    }
    else
    {
        LOGT("%s: Client '%s' already connected", ifname, client->mac);
        return;
    }

    if (key_id != NULL)
    {
        STRSCPY(client->key_id, key_id);
    }

    clients_update(ifname, client->mac, client->key_id, true);

    return;
}

void clients_disconnection(INT apIndex, char *mac)
{
    client_t        *client;
    char ifname[256];

    memset(ifname, 0, sizeof(ifname));

    if (wifi_getApName(apIndex, ifname) != RETURN_OK)
    {
        LOGE("%s: cannot get apName for index %d\n", __func__, apIndex);
        return;
    }

    if (mac == NULL)
    {
        return;
    }

    client = ds_tree_find(&connected_clients, mac);
    if (client)
    {
        if (client->apIndex != apIndex)
        {
            LOGI("%s: Client '%s' disconnect ignored (active on %d)",
                    ifname, client->mac, client->apIndex);
            return;
        }

        LOGI("%s: Client disconnected (%s)", ifname, mac);
        clients_update(ifname, mac, client ? client->key_id : NULL, false);
        ds_tree_remove(&connected_clients, client);
        free(client);
        return;
    }

    LOGI("%s: Client '%s; disconnect cb received, but client is not tracked, ignoring",
            ifname, mac);

    return;
}


