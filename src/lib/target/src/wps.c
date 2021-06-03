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
#include <ev.h>
#include <stdbool.h>

#include "target.h"
#include "target_internal.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

#define MODULE_ID LOG_MODULE_ID_MAIN

#define HAL_CB_QUEUE_MAX    20

typedef struct
{
    INT                     ssid_index;
    wifi_wps_t              event;
    ds_dlist_node_t         node;
} wps_event_t;

static struct ev_loop       *hal_cb_wps_loop = NULL;
static pthread_mutex_t      hal_cb_wps_lock;
static ev_async             hal_cb_wps_async;
static ds_dlist_t           hal_cb_wps_queue;
static int                  hal_cb_wps_queue_len = 0;


INT wps_hal_cb(INT ssid_index, wifi_wps_t event)
{
    wps_event_t          *cbe;
    INT                  ret = RETURN_ERR;

    pthread_mutex_lock(&hal_cb_wps_lock);

    if (hal_cb_wps_queue_len == HAL_CB_QUEUE_MAX)
    {
        LOGW("hal_cb_wps_queue: Queue is full! Ignoring event...");
        goto exit;
    }

    if ((cbe = calloc(1, sizeof(*cbe))) == NULL)
    {
        LOGE("%s: Failed to allocate memory!", __func__);
        goto exit;
    }
    cbe->ssid_index = ssid_index;
    cbe->event = event;

    ds_dlist_insert_tail(&hal_cb_wps_queue, cbe);
    hal_cb_wps_queue_len++;
    ret = RETURN_OK;

exit:
    pthread_mutex_unlock(&hal_cb_wps_lock);
    if (ret == RETURN_OK && hal_cb_wps_loop)
    {
        if (!ev_async_pending(&hal_cb_wps_async))
        {
            ev_async_send(hal_cb_wps_loop, &hal_cb_wps_async);
        }
    }
    return ret;
}

static bool radio_ifname_from_ssid_idx(INT ssid_index, char *radio_ifname)
{
    INT ret;
    INT radioIndex;

    ret = wifi_getSSIDRadioIndex(ssid_index, &radioIndex);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot getSSIDRadioIndex for ssid idx: %d", __func__, ssid_index);
        return false;
    }
    ret = wifi_getRadioIfName(radioIndex, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot getRadioIfName for radio idx %d", __func__, radioIndex);
        return false;
    }
    return true;
}

static void wps_pbc_vif_state_update(wps_event_t *cbe)
{
    char                         radio_ifname[256];
    struct schema_Wifi_VIF_State vstate;
    char                         ifname[256];

    if (wifi_getApName(cbe->ssid_index, ifname) != RETURN_OK)
    {
        LOGE("%s: cannot get AP name for index %d", __func__, cbe->ssid_index);
        return;
    }

    if (!radio_ifname_from_ssid_idx(cbe->ssid_index, radio_ifname))
    {
        LOGE("%s: Failed to get radio ifname for %s", __func__, ifname);
        return;
    }

    memset(&vstate, 0, sizeof(vstate));
    vstate._partial_update = true;
    SCHEMA_SET_STR(vstate.if_name, ifname);
    SCHEMA_SET_INT(vstate.wps_pbc, false);

    radio_rops_vstate(&vstate, radio_ifname);
}

static void wps_hal_async_cb(EV_P_ ev_async *w, int revents)
{
    ds_dlist_iter_t     qiter;
    wps_event_t        *cbe;
    char                ifname[256];

    pthread_mutex_lock(&hal_cb_wps_lock);
    cbe = ds_dlist_ifirst(&qiter, &hal_cb_wps_queue);
    while(cbe)
    {
        ds_dlist_iremove(&qiter);
        hal_cb_wps_queue_len--;

        memset(ifname, 0, sizeof(ifname));
        if (wifi_getApName(cbe->ssid_index, ifname) != RETURN_OK)
        {
            LOGE("%s: cannot get AP name for index %d", __func__, cbe->ssid_index);
            free(cbe);
            cbe = ds_dlist_inext(&qiter);
            continue;
        }

        LOGD("wps: received event %d, for ifname: %s", cbe->event, ifname);
        switch (cbe->event)
        {
            case WIFI_WPS_EVENT_TIMEOUT:
            case WIFI_WPS_EVENT_SUCCESS:
            case WIFI_WPS_EVENT_DISABLE:
                wps_pbc_vif_state_update(cbe);
                break;
            case WIFI_WPS_EVENT_ACTIVE:
            case WIFI_WPS_EVENT_OVERLAP:
            default:
                break;
        }

        free(cbe);
        cbe = ds_dlist_inext(&qiter);
    }
    pthread_mutex_unlock(&hal_cb_wps_lock);
}

void wps_hal_init()
{
    // Check if we've been called already
    if (hal_cb_wps_loop != NULL)
    {
        LOGE("%s: hal_cb_wps_loop already initialized", __func__);
        return;
    }

    // Save running evloop
    if (wifihal_evloop == NULL)
    {
        LOGE("wps_hal_init: Called before wifihal_evloop is initialized!");
        return;
    }
    hal_cb_wps_loop = wifihal_evloop;

    // Init CB Queue
    ds_dlist_init(&hal_cb_wps_queue, wps_event_t, node);
    hal_cb_wps_queue_len = 0;

    // Init mutex lock for queue
    pthread_mutex_init(&hal_cb_wps_lock, NULL);

    // Init async watcher
    ev_async_init(&hal_cb_wps_async, wps_hal_async_cb);
    ev_async_start(hal_cb_wps_loop, &hal_cb_wps_async);

    // Register callbacks (NOTE: calls callback from created pthread)
    wifi_apWps_callback_register(wps_hal_cb);
}

static void wps_pbc_vif_config_update(const char *ifname, const char *radio_ifname)
{
    struct schema_Wifi_VIF_Config vconf;

    memset(&vconf, 0, sizeof(vconf));
    SCHEMA_SET_STR(vconf.if_name, ifname);
    vconf._partial_update = true;
    vconf.wps_pbc_exists = false;
    vconf.wps_pbc_present = true;

    radio_rops_vconfig(&vconf, radio_ifname);
}

void wps_to_state(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    BOOL        wps = false;
    CHAR        buf[WIFIHAL_MAX_BUFFER];
    INT         ret;
    wifi_wps_t  status;

    ret = wifi_getApWpsEnable(ssid_index, &wps);
    if (ret != RETURN_OK)
    {
        LOGW("WPS: failed to getApWpsEnable for index %d", ssid_index);
    }
    else
    {
        SCHEMA_SET_INT(vstate->wps, wps);
    }

    ret = wifi_getApWpsButtonPushStatus(ssid_index, &status);
    if (ret != RETURN_OK)
    {
        LOGW("WPS: failed to getApWpsButtonPushStatus for index %d", ssid_index);
    }
    else
    {
        if (status == WIFI_WPS_EVENT_ACTIVE)
        {
            SCHEMA_SET_INT(vstate->wps_pbc, true);
        }
        else
        {
            SCHEMA_SET_INT(vstate->wps_pbc, false);
        }
    }

    memset(buf, 0, sizeof(buf));
    ret = wifi_getApWpsKeyID(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGW("WPS: failed to getApWpsKeyID for index %d", ssid_index);
    }
    else
    {
        SCHEMA_SET_STR(vstate->wps_pbc_key_id, buf);
    }
}


void vif_config_set_wps(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_VIF_Config_flags *changed,
        const char *radio_ifname)
{
    INT ret;
    CHAR key_id[64];

    if (changed->wps || changed->wps_pbc || changed->wps_pbc_key_id)
    {
        if (vconf->wps)
        {
            ret = wifi_cancelApWPS(ssid_index);
            if (ret != RETURN_OK)
            {
                LOGW("WPS: failed to cancelApWPS for index %d", ssid_index);
            }

            ret = wifi_setApWpsEnable(ssid_index, vconf->wps);
            if (ret != RETURN_OK)
            {
                LOGW("WPS: failed to setApWpsEnable to %d for index %d", vconf->wps, ssid_index);
            }
            else
            {
                ret = wifi_setApWpsConfigMethodsEnabled(ssid_index, "PushButton");
                if (ret != RETURN_OK)
                {
                    LOGW("WPS: failed to setApWpsConfigMethodsEnabled to PushButton for index %d", ssid_index);
                }
                else if (vconf->wps_pbc)
                {
                    memset(key_id, 0, sizeof(key_id));
                    memcpy(key_id, vconf->wps_pbc_key_id, sizeof(key_id));

                    ret = wifi_setApWpsButtonPushKeyID(ssid_index, key_id);
                    if (ret != RETURN_OK)
                    {
                        LOGW("WPS: failed to setApWpsButtonPush for key_id: %s, index %d", vconf->wps_pbc_key_id, ssid_index);
                    }
                }
            }
        }

        wps_pbc_vif_config_update(vconf->if_name, radio_ifname);
    }

}
