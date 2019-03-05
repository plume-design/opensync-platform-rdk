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
 * wifihal_clients_hal.c
 *
 * RDKB Platform - Wifi HAL - Clients through HAL APIs
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
#include "ds_dlist.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID           LOG_MODULE_ID_HAL

#define HAL_CB_QUEUE_MAX    20

/*****************************************************************************/

typedef struct
{
    INT                     ssid_index;
    wifi_associated_dev_t   sta;

    ds_dlist_node_t         node;
} hal_cb_entry_t;

static struct ev_loop      *hal_cb_loop = NULL;
static pthread_mutex_t      hal_cb_lock;
static ev_async             hal_cb_async;
static ds_dlist_t           hal_cb_queue;
static int                  hal_cb_queue_len = 0;

/*****************************************************************************/

// Don't know why this prototype is missing...
extern INT wifi_getAssociatedDeviceDetail(INT apIndex, INT devIndex, wifi_device_t *output_struct);

static bool
wifihal_clients_hal_fetch_existing(wifihal_ssid_t *ssid)
{
    wifi_device_t   wd;
    os_macaddr_t    macaddr;
    ULONG           num_devices = 0;
    ULONG           i;
    char            mac[20];
    INT             ret;

    WIFIHAL_TM_START();
    ret = wifi_getApNumDevicesAssociated(ssid->index, &num_devices);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApNumDevicesAssociated(%d) ret %ld",
                                         ssid->index, num_devices);
    if (ret != RETURN_OK) {
        LOGE("%s: Failed to fetch number of associated devices", ssid->ifname);
        return false;
    }
    LOGI("%s: Found %ld existing associated clients", ssid->ifname, num_devices);

    for (i = 0; i < num_devices; i++)
    {
        memset(&wd, 0, sizeof(wd));
        WIFIHAL_TM_START();
        ret = wifi_getAssociatedDeviceDetail(ssid->index, (i+1), &wd);
        if (ret != RETURN_OK)
        {
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getAssociatedDeviceDetail(%d, %ld)",
                                             ssid->index, (i+1));
            LOGE("%s: Failed to get details for associated dev #%ld",
                 ssid->ifname, i);
            continue;
        }

        memcpy(&macaddr, wd.wifi_devMacAddress, sizeof(macaddr));
        snprintf(mac, sizeof(mac)-1, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getAssociatedDeviceDetail(%d, %ld) ret \"%s\"",
                                             ssid->index, (i+1), (ret == RETURN_OK) ? mac : "");

        // Report its connection
        wifihal_clients_connection(ssid, mac, NULL);
    }

    return true;
}

static void
wifihal_clients_hal_async_cb(EV_P_ ev_async *w, int revents)
{
    ds_dlist_iter_t     qiter;
    hal_cb_entry_t      *cbe;
    wifihal_ssid_t      *ssid;
    os_macaddr_t        macaddr;
    char                mac[20];

    pthread_mutex_lock(&hal_cb_lock);

    cbe = ds_dlist_ifirst(&qiter, &hal_cb_queue);
    while (cbe)
    {
        ds_dlist_iremove(&qiter);
        hal_cb_queue_len--;

        memcpy(&macaddr, cbe->sta.cli_MACAddress, sizeof(macaddr));
        snprintf(mac, sizeof(mac)-1, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));

        if ((ssid = wifihal_ssid_by_index(cbe->ssid_index)))
        {
            if (cbe->sta.cli_Active)
            {
                wifihal_clients_connection(ssid, mac, NULL);
            }
            else
            {
                wifihal_clients_disconnection(ssid, mac);
            }
        }

        free(cbe);
        cbe = ds_dlist_inext(&qiter);
    }

    pthread_mutex_unlock(&hal_cb_lock);
    return;
}

INT
wifihal_clients_hal_assocdev_cb(INT ssid_index, wifi_associated_dev_t *sta)
{
    hal_cb_entry_t      *cbe;
    INT                 ret = RETURN_ERR;

    pthread_mutex_lock(&hal_cb_lock);

    if (hal_cb_queue_len == HAL_CB_QUEUE_MAX) {
        LOGW("wifihal_clients_hal_assocdev_cb: Queue is full! Ignoring event...");
        goto exit;
    }

    if (!(cbe = calloc(1, sizeof(*cbe)))) {
        LOGE("wifihal_clients_hal_assocdev_cb: Failed to allocate memory!");
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


/*****************************************************************************/

bool
wifihal_clients_hal_init(void)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ds_dlist_t          *radios;

    // See if we've been called already
    if (hal_cb_loop) {
        // Already was initialized, just [re]start async watcher
        ev_async_start(hal_cb_loop, &hal_cb_async);
        return true;
    }

    // Save running evloop
    if (!wifihal_evloop) {
        LOGE("wifihal_clients_hal_init: Called before wifihal_evloop is initialized!");
        return false;
    }
    hal_cb_loop = wifihal_evloop;

    // Init CB Queue
    ds_dlist_init(&hal_cb_queue, hal_cb_entry_t, node);
    hal_cb_queue_len = 0;

    // Init mutex lock for queue
    pthread_mutex_init(&hal_cb_lock, NULL);

    // Init async watcher
    ev_async_init(&hal_cb_async, wifihal_clients_hal_async_cb);
    ev_async_start(hal_cb_loop, &hal_cb_async);

    // Register callback (NOTE: calls callback from created pthread)
    WIFIHAL_TM_START();
    wifi_newApAssociatedDevice_callback_register(wifihal_clients_hal_assocdev_cb);
    WIFIHAL_TM_STOP(RETURN_OK, WIFIHAL_STD_TIME, "wifi_newApAssociatedDevice_callback_register()");

    // Fetch existing clients
    if (!(radios = wifihal_get_radios())) {
        return false;
    }
    ds_dlist_foreach(radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            if (!wifihal_clients_hal_fetch_existing(ssid)) {
                return false;
            }
        }
    }

    return true;
}

bool
wifihal_clients_hal_cleanup(void)
{
    // Cannot "unregister" callback, so just stop async watcher
    ev_async_stop(hal_cb_loop, &hal_cb_async);

    return true;
}
