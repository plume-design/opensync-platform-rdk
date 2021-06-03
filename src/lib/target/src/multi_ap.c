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

#include <ev.h>

#include "target.h"
#include "target_internal.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

#define MODULE_ID LOG_MODULE_ID_MAIN

#define HAL_CB_QUEUE_MAX    20

static c_item_t map_device_type[] =
{
    C_ITEM_STR(WIFI_MULTI_AP_NONE,                   "none"),
    C_ITEM_STR(WIFI_MULTI_AP_FRONTHAUL_BSS,          "fronthaul_bss"),
    C_ITEM_STR(WIFI_MULTI_AP_BACKHAUL_BSS,           "backhaul_bss"),
    C_ITEM_STR(WIFI_MULTI_AP_FRONTHAUL_BACKHAUL_BSS, "fronthaul_backhaul_bss"),
    C_ITEM_STR(WIFI_MULTI_AP_BACKHAUL_STA,           "backhaul_sta"),
};

typedef struct
{
    INT                     ssid_index;
    wifi_multiApVlanEvent_t event;
    ds_dlist_node_t         node;
} multi_ap_event_t;

static struct ev_loop       *hal_cb_multi_ap_loop = NULL;
static pthread_mutex_t      hal_cb_multi_ap_lock;
static ev_async             hal_cb_multi_ap_async;
static ds_dlist_t           hal_cb_multi_ap_queue;
static int                  hal_cb_multi_ap_queue_len = 0;

INT multi_ap_hal_cb(INT apIndex, wifi_multiApVlanEvent_t event)
{
    multi_ap_event_t         *cbe;
    INT                      ret = RETURN_ERR;

    pthread_mutex_lock(&hal_cb_multi_ap_lock);

    if (hal_cb_multi_ap_queue_len == HAL_CB_QUEUE_MAX)
    {
        LOGW("%s_queue: Queue is full! Ignoring event...", __func__);
        goto exit;
    }

    if ((cbe = calloc(1, sizeof(*cbe))) == NULL)
    {
        LOGE("%s: Failed to allocate memory!", __func__);
        goto exit;
    }
    cbe->ssid_index = apIndex;
    cbe->event = event;

    ds_dlist_insert_tail(&hal_cb_multi_ap_queue, cbe);
    hal_cb_multi_ap_queue_len++;
    ret = RETURN_OK;

exit:
    pthread_mutex_unlock(&hal_cb_multi_ap_lock);
    if (ret == RETURN_OK && hal_cb_multi_ap_loop)
    {
        if (!ev_async_pending(&hal_cb_multi_ap_async))
        {
            ev_async_send(hal_cb_multi_ap_loop, &hal_cb_multi_ap_async);
        }
    }
    return ret;
}

static void multi_ap_vif_state_update(multi_ap_event_t *cbe)
{
    char                         radio_ifname[256];
    struct schema_Wifi_VIF_State vstate;
    char                         ifname[256];

    if (wifi_getApName(cbe->ssid_index, ifname) != RETURN_OK)
    {
        LOGE("%s: cannot get AP name for index %d", __func__, cbe->ssid_index);
        return;
    }

    if (!vif_get_radio_ifname(cbe->ssid_index, radio_ifname, sizeof(radio_ifname)))
    {
        LOGE("%s: Failed to get radio ifname for %s", __func__, ifname);
        return;
    }
    if (!vif_state_get(cbe->ssid_index, &vstate))
    {
        LOGE("%s: cannot get vif state for SSID index %d", __func__, cbe->ssid_index);
        return;
    }

    radio_rops_vstate(&vstate, radio_ifname);
}

static void multi_ap_hal_async_cb(EV_P_ ev_async *w, int revents)
{
    ds_dlist_iter_t     qiter;
    multi_ap_event_t    *cbe;

    pthread_mutex_lock(&hal_cb_multi_ap_lock);
    cbe = ds_dlist_ifirst(&qiter, &hal_cb_multi_ap_queue);
    while(cbe)
    {
        ds_dlist_iremove(&qiter);
        hal_cb_multi_ap_queue_len--;

        LOGI("multi_ap: received event %d, for index: %d", cbe->event, cbe->ssid_index);
        multi_ap_vif_state_update(cbe);

        free(cbe);
        cbe = ds_dlist_inext(&qiter);
    }
    pthread_mutex_unlock(&hal_cb_multi_ap_lock);
}

void multi_ap_hal_init()
{
    // Check if we've been called already
    if (hal_cb_multi_ap_loop != NULL)
    {
        LOGE("%s: hal_cb_multi_ap_loop already initialized", __func__);
        return;
    }

    // Save running evloop
    if (wifihal_evloop == NULL)
    {
        LOGE("wps_hal_init: Called before wifihal_evloop is initialized!");
        return;
    }
    hal_cb_multi_ap_loop = wifihal_evloop;

    // Init CB Queue
    ds_dlist_init(&hal_cb_multi_ap_queue, multi_ap_event_t, node);
    hal_cb_multi_ap_queue_len = 0;

    // Init mutex lock for queue
    pthread_mutex_init(&hal_cb_multi_ap_lock, NULL);

    // Init async watcher
    ev_async_init(&hal_cb_multi_ap_async, multi_ap_hal_async_cb);
    ev_async_start(hal_cb_multi_ap_loop, &hal_cb_multi_ap_async);

    // Register callbacks (NOTE: calls callback from created pthread)
    wifi_multiAp_callback_register(multi_ap_hal_cb);
}

void vif_config_set_multi_ap(
        INT ssid_index,
        const char *multi_ap,
        const struct schema_Wifi_VIF_Config_flags *changed)
{
    INT ret;

    if (changed->multi_ap)
    {
        ret = wifi_setMultiApDeviceType(ssid_index,
                  !strcmp(multi_ap, "none") ? WIFI_MULTI_AP_NONE :
                  !strcmp(multi_ap, "fronthaul_bss") ? WIFI_MULTI_AP_FRONTHAUL_BSS :
                  !strcmp(multi_ap, "backhaul_bss") ? WIFI_MULTI_AP_BACKHAUL_BSS :
                  !strcmp(multi_ap, "fronthaul_backhaul_bss") ? WIFI_MULTI_AP_FRONTHAUL_BACKHAUL_BSS :
                  !strcmp(multi_ap, "backhaul_sta") ? WIFI_MULTI_AP_BACKHAUL_STA :
                  WIFI_MULTI_AP_NONE);
        if (ret != RETURN_OK)
        {
            LOGW("Multi AP: failed to setMultiApDeviceType to %s for index: %d", multi_ap, ssid_index);
            return;
        }
        LOGD("Multi AP: Device type successfully changed to %s for index: %d", multi_ap, ssid_index);
    }
}

void multi_ap_to_state(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    INT                      ret;
    wifi_multiApDeviceType_t device_type;
    c_item_t                 *citem;
    char                     *device_type_str;
    CHAR                     macSta[WIFIHAL_MAX_MACSTR];
    wifi_mode_t              mode;

    ret = wifi_getMultiApDeviceType(ssid_index, &device_type);
    if (ret != RETURN_OK)
    {
        LOGW("Multi AP: failed to getMultiApDeviceType for index: %d", ssid_index);
        return;
    }

    if ((citem = c_get_item_by_key(map_device_type, device_type)) != NULL)
    {
        device_type_str = c_get_str_by_key(map_device_type, device_type);
        SCHEMA_SET_STR(vstate->multi_ap, device_type_str);
    }
    else
    {
        LOGW("Multi AP: Received unrecognized device type: %d for index: %d", device_type, ssid_index);
    }

    memset(macSta, 0, sizeof(macSta));
    ret = wifi_getMultiApVlanStaAddr(ssid_index, macSta);
    if (ret != RETURN_OK)
    {
        LOGW("Multi AP: Failed to getMultiApVlanStaAddr for index: %d", ssid_index);
    }
    else
    {
        SCHEMA_SET_STR(vstate->ap_vlan_sta_addr, macSta);
    }

    ret = wifi_getApMode(ssid_index, &mode);
    if (ret != RETURN_OK)
    {
        LOGW("Multi AP: Failed to getApMode for index: %d", ssid_index);
    }
    else
    {
        if (mode == MODE_AP_VLAN)
        {
            SCHEMA_SET_STR(vstate->mode, "ap_vlan");
        }
    }
}
