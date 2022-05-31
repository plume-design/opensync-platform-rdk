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

#include "target.h"
#include "ds_tree.h"
#include "target_internal.h"

#ifdef CONFIG_RDK_HAS_ASSOC_REQ_IES
#include <endian.h>
#include "bm_ieee80211.h"
#endif

/*****************************************************************************/

#define WIFI_HAL_STR_LEN  64
#define BTM_DEFAULT_PREF 1

typedef enum {
    BSAL_BAND_UNINITIALIZED = 0,
    BSAL_BAND_24G,
    BSAL_BAND_5G,
    BSAL_BAND_6G
} bsal_band_t;

typedef struct
{
    bsal_band_t band;
    bsal_ifconfig_t bsal_cfg;
    wifi_steering_apConfig_t wifihal_cfg;
} iface_t;

typedef struct
{
    UINT index;
    size_t iface_number;
    iface_t *iface;
} bsal_group_t;

typedef struct
{
    uint8_t bssid[BSAL_MAC_ADDR_LEN];
    char ifname[BSAL_IFNAME_LEN];
} bsal_neigh_key_t;

typedef struct
{
    bsal_neigh_key_t    key;
    bsal_neigh_info_t   nr;
    ds_tree_node_t      dst_node;
} bsal_neighbor_t;

typedef enum {
    BSAL_CHAN_WIDTH_20 = 0,
    BSAL_CHAN_WIDTH_40,
    BSAL_CHAN_WIDTH_80,
    BSAL_CHAN_WIDTH_160,
    BSAL_CHAN_WIDTH_UNSUPPORTED
} bsal_chwidth_t;

static ds_key_cmp_t bssid_ifname_cmp;
static ds_tree_t    bsal_ifaces_neighbors = DS_TREE_INIT(bssid_ifname_cmp,
                                                            bsal_neighbor_t,
                                                            dst_node);

static bsal_event_cb_t _bsal_event_cb = NULL;

/*
 * At the moment we support only devices with two VAPs because we don't have
 * RDK-based box with 3+ VAPs.
 */
static bsal_group_t group;

static c_item_t map_disc_source[] =
{
    C_ITEM_VAL(DISCONNECT_SOURCE_LOCAL, BSAL_DISC_SOURCE_LOCAL),
    C_ITEM_VAL(DISCONNECT_SOURCE_REMOTE, BSAL_DISC_SOURCE_REMOTE)
};

static c_item_t map_disc_type[] =
{
    C_ITEM_VAL(DISCONNECT_TYPE_DISASSOC, BSAL_DISC_TYPE_DISASSOC),
    C_ITEM_VAL(DISCONNECT_TYPE_DEAUTH, BSAL_DISC_TYPE_DEAUTH)
};

static c_item_t map_rssi_xing[] =
{
    C_ITEM_VAL(WIFI_STEERING_RSSI_UNCHANGED, BSAL_RSSI_UNCHANGED),
    C_ITEM_VAL(WIFI_STEERING_RSSI_LOWER, BSAL_RSSI_LOWER),
    C_ITEM_VAL(WIFI_STEERING_RSSI_HIGHER, BSAL_RSSI_HIGHER)
};

typedef struct
{
    uint8_t             mac[BSAL_MAC_ADDR_LEN];
    bsal_client_info_t  client;
    ds_dlist_node_t     node;
} bsal_client_info_cache_t;

static ds_dlist_t g_client_info_list = DS_DLIST_INIT(
        bsal_client_info_cache_t,
        node);

/*****************************************************************************/

static bsal_client_info_cache_t *bsal_find_client_info(const uint8_t *mac)
{
    bsal_client_info_cache_t *client_info_cache;

    ds_dlist_foreach(&g_client_info_list, client_info_cache)
    {
        if (!memcmp(mac, client_info_cache->mac, BSAL_MAC_ADDR_LEN))
        {
            return client_info_cache;
        }
    }

    LOGD("BSAL %02x:%02x:%02x:%02x:%02x:%02x client_info not found",
               mac[0], mac[1], mac[2],
               mac[3], mac[4], mac[5]);

    return NULL;
}

static UINT bsal_convert_max_chwidth(UINT max_chwidth)
{
    if (max_chwidth <= 3) return max_chwidth;

    if (max_chwidth == 20) return BSAL_CHAN_WIDTH_20;
    if (max_chwidth == 40) return BSAL_CHAN_WIDTH_40;
    if (max_chwidth == 80) return BSAL_CHAN_WIDTH_80;
    if (max_chwidth == 160) return BSAL_CHAN_WIDTH_160;

    return BSAL_CHAN_WIDTH_UNSUPPORTED;
}

static void bsal_client_info_update(const wifi_steering_evConnect_t *connect)
{
    bsal_client_info_cache_t *client_info_cache;
    UINT max_chanwidth;

    client_info_cache = bsal_find_client_info(connect->client_mac);
    if (client_info_cache == NULL)  /* Allocate new node */
    {
        client_info_cache = (bsal_client_info_cache_t *)calloc(1, sizeof(*client_info_cache));
        if (client_info_cache == NULL)
        {
            LOGE("BSAL Failed to allocate memory for new client info");
            return;
        }
        memcpy(client_info_cache->mac, connect->client_mac, sizeof(client_info_cache->mac));
        ds_dlist_insert_tail(&g_client_info_list, client_info_cache);
    }

    client_info_cache->client.is_BTM_supported = connect->isBTMSupported;
    client_info_cache->client.is_RRM_supported = connect->isRRMSupported;
    switch (connect->bandsCap)
    {
        case WIFI_FREQUENCY_2_4_BAND:
            client_info_cache->client.band_cap_2G = true;
            break;

        case WIFI_FREQUENCY_5_BAND:
        case WIFI_FREQUENCY_5L_BAND:
        case WIFI_FREQUENCY_5H_BAND:
            client_info_cache->client.band_cap_5G = true;
            break;

        case WIFI_FREQUENCY_6_BAND:
        case WIFI_FREQUENCY_60_BAND:
            client_info_cache->client.band_cap_6G = true;
            break;
    }

    max_chanwidth = bsal_convert_max_chwidth(connect->datarateInfo.maxChwidth);
    if (max_chanwidth == BSAL_CHAN_WIDTH_UNSUPPORTED)
    {
        LOGW("Client "MAC_ADDR_FMT": unsupported maximum channel width %u",
                MAC_ADDR_UNPACK(client_info_cache->mac), connect->datarateInfo.maxChwidth);
        client_info_cache->client.datarate_info.max_chwidth = connect->datarateInfo.maxChwidth;
    }
    else
    {
        client_info_cache->client.datarate_info.max_chwidth = max_chanwidth;
    }
    client_info_cache->client.datarate_info.max_streams = connect->datarateInfo.maxStreams;
    client_info_cache->client.datarate_info.phy_mode = connect->datarateInfo.phyMode;
    client_info_cache->client.datarate_info.max_MCS = connect->datarateInfo.maxMCS;
    client_info_cache->client.datarate_info.max_txpower = connect->datarateInfo.maxTxpower;
    client_info_cache->client.datarate_info.is_static_smps = connect->datarateInfo.isStaticSmps;
    client_info_cache->client.datarate_info.is_mu_mimo_supported = connect->datarateInfo.isMUMimoSupported;
    client_info_cache->client.rrm_caps.link_meas = connect->rrmCaps.linkMeas;
    client_info_cache->client.rrm_caps.neigh_rpt = connect->rrmCaps.neighRpt;
    client_info_cache->client.rrm_caps.bcn_rpt_passive = connect->rrmCaps.bcnRptPassive;
    client_info_cache->client.rrm_caps.bcn_rpt_active = connect->rrmCaps.bcnRptActive;
    client_info_cache->client.rrm_caps.bcn_rpt_table = connect->rrmCaps.bcnRptTable;
    client_info_cache->client.rrm_caps.lci_meas = connect->rrmCaps.lciMeas;
    client_info_cache->client.rrm_caps.ftm_range_rpt = connect->rrmCaps.ftmRangeRpt;

    /* assoc_ies and assoc_ies_len set to 0 */
}

static void bsal_client_info_remove(const uint8_t *mac)
{
    bsal_client_info_cache_t *client_info_cache;

    client_info_cache = bsal_find_client_info(mac);
    if (client_info_cache == NULL)
    {
        return;
    }
    ds_dlist_remove(&g_client_info_list, client_info_cache);
    free(client_info_cache);
}

static void process_event(
        UINT steeringgroupIndex,
        wifi_steering_event_t *wifi_hal_event)
{
    bsal_event_t *bsal_event = NULL;
    uint32_t val = 0;
    size_t i;

    // If we don't have a callback, just ignore the data
    if (_bsal_event_cb == NULL)
    {
        goto end;
    }

    bsal_event = (bsal_event_t *)calloc(1, sizeof(*bsal_event));
    if (bsal_event == NULL)
    {
        LOGE("BSAL Failed to allocate memory for new event");
        goto end;
    }

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band != BSAL_BAND_UNINITIALIZED &&
            group.iface[i].wifihal_cfg.apIndex == wifi_hal_event->apIndex)
        {
            STRSCPY(bsal_event->ifname, group.iface[i].bsal_cfg.ifname);
            break;
        }
    }

    if (bsal_event->ifname == NULL)
    {
        LOGD("BSAL Dropping event received for unknown iface (apIndex: %d)", wifi_hal_event->apIndex);
        goto end;
    }

    memset(bsal_event->data.connect.assoc_ies, 0, sizeof(bsal_event->data.connect.assoc_ies));
    bsal_event->data.connect.assoc_ies_len = 0;

    switch (wifi_hal_event->type)
    {
    case WIFI_STEERING_EVENT_PROBE_REQ:
        LOGT("Event: WIFI_STEERING_EVENT_PROBE_REQ");
        bsal_event->type = BSAL_EVENT_PROBE_REQ;
        memcpy(&bsal_event->data.probe_req.client_addr,
               &wifi_hal_event->data.probeReq.client_mac,
               sizeof(bsal_event->data.probe_req.client_addr));
        bsal_event->data.probe_req.rssi = wifi_hal_event->data.probeReq.rssi;
        bsal_event->data.probe_req.ssid_null = wifi_hal_event->data.probeReq.broadcast ? true : false;
        bsal_event->data.probe_req.blocked = wifi_hal_event->data.probeReq.blocked   ? true : false;
        break;

    case WIFI_STEERING_EVENT_AUTH_FAIL:
        LOGT("Event: WIFI_STEERING_EVENT_AUTH_FAIL");
        /*
         * This event happens any time an auth request has failed.
         * This includes normal failures, as well as when band steering has
         * requested it be blocked.
         *
         * To determine if it's due to band steering blocking it, check that:
         *      bsdev->data.bs_auth.bs_blocked == 1
         *
         * To determine if a reject was sent due to band steering, check:
         *      bsdev->data.bs_auth.bs_rejected == 1
         *
         * When bs_rejected == 1, bs_blocked is also == 1.
         * Hence, for reject detection purposes, count bs_blocked.
         */
        bsal_event->type = BSAL_EVENT_AUTH_FAIL;
        memcpy(&bsal_event->data.auth_fail.client_addr,
               &wifi_hal_event->data.authFail.client_mac,
               sizeof(bsal_event->data.auth_fail.client_addr));
        bsal_event->data.auth_fail.rssi = wifi_hal_event->data.authFail.rssi;
        bsal_event->data.auth_fail.reason = wifi_hal_event->data.authFail.reason;
        bsal_event->data.auth_fail.bs_blocked = wifi_hal_event->data.authFail.bsBlocked;
        bsal_event->data.auth_fail.bs_rejected = wifi_hal_event->data.authFail.bsRejected;
        break;

    case WIFI_STEERING_EVENT_CLIENT_CONNECT:
        LOGT("Event: WIFI_STEERING_EVENT_CLIENT_CONNECT");
        bsal_event->type = BSAL_EVENT_CLIENT_CONNECT;
        memcpy(&bsal_event->data.connect.client_addr,
               &wifi_hal_event->data.connect.client_mac,
               sizeof(bsal_event->data.connect.client_addr));

        bsal_client_info_update(&wifi_hal_event->data.connect);
        break;

    case WIFI_STEERING_EVENT_CLIENT_DISCONNECT:
        LOGT("Event: WIFI_STEERING_EVENT_CLIENT_DISCONNECT");
        bsal_event->type = BSAL_EVENT_CLIENT_DISCONNECT;
        memcpy(&bsal_event->data.disconnect.client_addr,
               &wifi_hal_event->data.disconnect.client_mac,
               sizeof(bsal_event->data.disconnect.client_addr));

        if (!c_get_value_by_key(map_disc_source, wifi_hal_event->data.disconnect.source, &val))
        {
            LOGE("BSAL process_event(WIFI_STEERING_EVENT_CLIENT_DISCONNECT): "
                 "Unknown source %d", wifi_hal_event->data.disconnect.source);
            goto end;
        }
        bsal_event->data.disconnect.source = val;

        if (!c_get_value_by_key(map_disc_type, wifi_hal_event->data.disconnect.type, &val))
        {
            LOGE("BSAL process_event(WIFI_STEERING_EVENT_CLIENT_DISCONNECT): "
                 "Unknown type %d", wifi_hal_event->data.disconnect.type);
            goto end;
        }
        bsal_event->data.disconnect.type = val;

        bsal_event->data.disconnect.reason = wifi_hal_event->data.disconnect.reason;

        bsal_client_info_remove(wifi_hal_event->data.disconnect.client_mac);
        break;

    case WIFI_STEERING_EVENT_CLIENT_ACTIVITY:
        LOGT("Event: WIFI_STEERING_EVENT_CLIENT_ACTIVITY");
        bsal_event->type = BSAL_EVENT_CLIENT_ACTIVITY;
        memcpy(&bsal_event->data.activity.client_addr,
               &wifi_hal_event->data.activity.client_mac,
               sizeof(bsal_event->data.activity.client_addr));
        bsal_event->data.activity.active = wifi_hal_event->data.activity.active ? true : false;
        break;

    case WIFI_STEERING_EVENT_CHAN_UTILIZATION:
        LOGT("Event: WIFI_STEERING_EVENT_CHAN_UTILIZATION");
        bsal_event->type = BSAL_EVENT_CHAN_UTILIZATION;
        bsal_event->data.chan_util.utilization = wifi_hal_event->data.chanUtil.utilization;
        break;

    case WIFI_STEERING_EVENT_RSSI_XING:
        LOGT("Event: WIFI_STEERING_EVENT_RSSI_XING");
        bsal_event->type = BSAL_EVENT_RSSI_XING;
        memcpy(&bsal_event->data.rssi_change.client_addr,
               &wifi_hal_event->data.rssiXing.client_mac,
               sizeof(bsal_event->data.rssi_change.client_addr));
        bsal_event->data.rssi_change.rssi = wifi_hal_event->data.rssiXing.rssi;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.inactveXing, &val))
        {
            LOGE("BSAL process_event(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown inact %d", wifi_hal_event->data.rssiXing.inactveXing);
            goto end;
        }
        bsal_event->data.rssi_change.inact_xing = val;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.highXing, &val))
        {
            LOGE("BSAL process_event(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown high %d", wifi_hal_event->data.rssiXing.highXing);
            goto end;
        }
        bsal_event->data.rssi_change.high_xing = val;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.lowXing, &val))
        {
            LOGE("BSAL process_event(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown low %d", wifi_hal_event->data.rssiXing.lowXing);
            goto end;
        }
        bsal_event->data.rssi_change.low_xing = val;
        break;

    case WIFI_STEERING_EVENT_RSSI:
        LOGT("Event: WIFI_STEERING_EVENT_RSSI");
        bsal_event->type = BSAL_EVENT_RSSI;
        memcpy(&bsal_event->data.rssi.client_addr,
               &wifi_hal_event->data.rssi.client_mac,
               sizeof(bsal_event->data.rssi.client_addr));
        bsal_event->data.rssi.rssi = wifi_hal_event->data.rssi.rssi;
        break;

    default:
        LOGI("Event: UNKNOWN");
        // Ignore this event
        goto end;
    }

    _bsal_event_cb(bsal_event);

end:
    // Free the memory allocated for event here, as _bsal_event_cb will
    // allocate the memory and copy the contents
    free(bsal_event);
}

static bool lookup_ifname(
        const bsal_ifconfig_t *ifcfg,
        iface_t *iface)
{
    CHAR buf[WIFI_HAL_STR_LEN];
    ULONG snum;
    UINT s;
    INT radio_index;
    int ret;
    BOOL enabled = false;
    wifi_radio_operationParam_t radio_params;

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Unable to get number of VAPs (wifi_getSSIDNumberOfEntries() failed with code %d)",
             ret);
        goto error;
    }

    if (snum == 0)
    {
        LOGE("BSAL No VAPs detected");
        goto error;
    }

    for (s = 0; s < snum; s++)
    {
        ret = wifi_getSSIDEnable(s, &enabled);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get SSID enabled state for index %d. Skipping", __func__, s);
            continue;
        }

        // Silently skip ifaces that are not enabled
        if (enabled == false) continue;

        memset(buf, 0, sizeof(buf));
        ret = wifi_getApName(s, buf);
        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to get ifname of VAO #%u (wifi_getApName() failed with code %d)",
                 s, ret);
            goto error;
        }

        // Silentely skip VAPs that are not controlled by OpenSync
        if (!vap_controlled(buf)) continue;

        if (strcmp(ifcfg->ifname, buf) != 0)
        {
            continue;
        }

        break;
    }

    if (s == snum)
    {
        LOGE("BSAL Radio with %s ifname was not found", ifcfg->ifname);
        goto error;
    }

    ret = wifi_getSSIDRadioIndex(s, &radio_index);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Unable to get radio index (wifi_getSSIDRadioIndex() failed with code %d)",
             ret);
        goto error;
    }

    memset(buf, 0, sizeof(buf));
    ret = wifi_getRadioOperatingParameters(radio_index, &radio_params);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to get operating freq of radio #%d "
            "(wifi_getRadioOperatingParameters() failed with code %d)",
             radio_index, ret);
        goto error;
    }

    switch (radio_params.band)
    {
        case WIFI_FREQUENCY_2_4_BAND:
            iface->band = BSAL_BAND_24G;
            break;

        case WIFI_FREQUENCY_5_BAND:
        case WIFI_FREQUENCY_5L_BAND:
        case WIFI_FREQUENCY_5H_BAND:
            iface->band = BSAL_BAND_5G;
            break;

        case WIFI_FREQUENCY_6_BAND:
        case WIFI_FREQUENCY_60_BAND:
            iface->band = BSAL_BAND_6G;
            break;
    }

    memcpy(&iface->bsal_cfg, ifcfg, sizeof(iface->bsal_cfg));

    iface->wifihal_cfg.apIndex = s;
    iface->wifihal_cfg.utilCheckIntervalSec = iface->bsal_cfg.chan_util_check_sec;
    iface->wifihal_cfg.utilAvgCount = iface->bsal_cfg.chan_util_avg_count;
    iface->wifihal_cfg.inactCheckIntervalSec = iface->bsal_cfg.inact_check_sec;
    iface->wifihal_cfg.inactCheckThresholdSec = iface->bsal_cfg.inact_tmout_sec_normal;

    LOGI("BSAL Found apIndex #%d (ifname: %s, band: %d)", iface->wifihal_cfg.apIndex,
         iface->bsal_cfg.ifname, iface->band);

    return true;

error:
    return false;
}

static iface_t* group_get_iface_by_name(const char *ifname)
{
    size_t i;

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band != BSAL_BAND_UNINITIALIZED &&
            (strcmp(ifname, group.iface[i].bsal_cfg.ifname) == 0))
            return &group.iface[i];
    }

    return NULL;
}

static bool group_add_iface(const iface_t *iface)
{
    size_t i;

    if (group_get_iface_by_name(iface->bsal_cfg.ifname) != NULL)
    {
        LOGE("BSAL iface: %s is already configured", iface->bsal_cfg.ifname);
        return false;
    }
    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band == BSAL_BAND_UNINITIALIZED)
        {
            memcpy(&group.iface[i], &iface, sizeof(group.iface[i]));
            break;
        }
    }
    if (i == group.iface_number)
    {
        LOGW("BSAL %s: maximum number of radios exceeded", iface->bsal_cfg.ifname);
        return false;
    }

    return true;
}

static bool is_group_initialized()
{
    size_t i;

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band == BSAL_BAND_UNINITIALIZED) return false;
    }
    return true;
}

static bool is_group_uninitialized()
{
    size_t i;

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band != BSAL_BAND_UNINITIALIZED) return false;
    }
    return true;
}

static bool group_update_iface(const iface_t *iface)
{
    size_t i;

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band == iface->band)
        {
            memcpy(&group.iface[i], iface, sizeof(group.iface[i]));
            return true;
        }
    }

    LOGE("BSAL %s iface is not already configured", iface->bsal_cfg.ifname);
    return false;
}

static bool group_remove_iface(const iface_t *iface)
{
    size_t i;

    for (i = 0; i < group.iface_number; i++)
    {
        if (group.iface[i].band == iface->band)
        {
            memset(&group.iface[i], 0, sizeof(group.iface[i]));
            return true;
        }
    }

    LOGE("BSAL %s iface is not configured", iface->bsal_cfg.ifname);
    return false;

}

static bool create_wifihal_ap_config_list(wifi_steering_apConfig_t **wifihal_cfg)
{
    size_t i;

    *wifihal_cfg = calloc(group.iface_number, sizeof(wifi_steering_apConfig_t));
    if (wifihal_cfg == NULL)
    {
        LOGE("BSAL Failed to allocate memory for wifihal ap config");
        return false;
    }

    for (i = 0; i < group.iface_number; i++)
    {
        memcpy(*wifihal_cfg, &group.iface[i].wifihal_cfg, sizeof(wifi_steering_apConfig_t));
    }

    return true;
}

static void bsal_client_to_wifi_steering_client(
        const bsal_client_config_t *bsal_cfg,
        wifi_steering_clientConfig_t *wifihal_cfg)
{
    memset(wifihal_cfg, 0, sizeof(*wifihal_cfg));
    wifihal_cfg->rssiProbeHWM = bsal_cfg->rssi_probe_hwm;
    wifihal_cfg->rssiProbeLWM = bsal_cfg->rssi_probe_lwm;
    wifihal_cfg->rssiAuthHWM = bsal_cfg->rssi_auth_hwm;
    wifihal_cfg->rssiAuthLWM = bsal_cfg->rssi_auth_lwm;
    wifihal_cfg->rssiInactXing = bsal_cfg->rssi_inact_xing;
    wifihal_cfg->rssiHighXing = bsal_cfg->rssi_high_xing;
    wifihal_cfg->rssiLowXing = bsal_cfg->rssi_low_xing;
    wifihal_cfg->authRejectReason = bsal_cfg->auth_reject_reason;
}

/*****************************************************************************/

#ifdef CONFIG_RDK_MGMT_FRAME_CB_SUPPORT
static INT mgmt_frame_cb(INT apIndex, UCHAR *sta_mac, UCHAR *frame, UINT len, wifi_mgmtFrameType_t type, wifi_direction_t dir)
{
    INT ret;
    bsal_event_t event;
    CHAR ifname[WIFI_HAL_STR_LEN];

    if (type != WIFI_MGMT_FRAME_TYPE_ACTION) return RETURN_OK; // Currently we support action frames only
    if (dir != wifi_direction_uplink) return RETURN_OK; // We only listen for received frames
    if (len >= BSAL_MAX_ACTION_FRAME_LEN)
    {
        LOGN("Skipping action frame because it's too big (%d vs %d)", len, BSAL_MAX_ACTION_FRAME_LEN);
        return RETURN_ERR;
    }

    LOGT("Received action frame, apIndex=%d, len=%u", apIndex, len);

    memset(ifname, 0, sizeof(ifname));
    ret = wifi_getApName(apIndex, ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get ifname of VAP #%u (wifi_getApName() failed with code %d)",
                __func__, apIndex, ret);
        return RETURN_ERR;
    }

    memset(&event, 0, sizeof(event));
    event.type = BSAL_EVENT_ACTION_FRAME;
    STRSCPY(event.ifname, ifname);
    memcpy(event.data.action_frame.data, frame, len);
    event.data.action_frame.data_len = len;

    _bsal_event_cb(&event);

    return RETURN_OK;
}
#endif

int target_bsal_init(
        bsal_event_cb_t event_cb,
        struct ev_loop *loop)
{
    int ret;
    ULONG rnum;

    if (_bsal_event_cb != NULL)
    {
        LOGW("BSAL callback already initialized");
        goto error;
    }

    _bsal_event_cb = event_cb;
    memset(&group, 0, sizeof(group));

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        goto error;
    }

    group.iface_number = rnum;
    group.iface = calloc(group.iface_number, sizeof(iface_t));
    if (!group.iface)
    {
        LOGE("%s:%d: unable to allocate memory", __func__, __LINE__);
        goto error;
    }

    // Register the callback
    ret = wifi_steering_eventRegister(process_event);
    if (ret < 0)
    {
        LOGE("BSAL Event callback registration failed (wifi_steering_eventRegister() "
             "failed with code %d)", ret);
        goto error;
    }

#ifdef CONFIG_RDK_MGMT_FRAME_CB_SUPPORT
    if (wifi_mgmt_frame_callbacks_register(mgmt_frame_cb) != RETURN_OK)
    {
        LOGE("wifi_mgmt_frame_callbacks_register FAILED");
        return -1;
    }
#endif
    LOGI("BSAL initialized");

    return 0;

error:
    return -1;
}

int target_bsal_cleanup(void)
{
    wifi_steering_eventUnregister();

    _bsal_event_cb = NULL;
    free(group.iface);

    LOGI("BSAL cleaned up");

    return 0;
}

int target_bsal_iface_add(const bsal_ifconfig_t *ifcfg)
{
    iface_t iface;
    wifi_steering_apConfig_t *wifihal_cfg;

    if (!lookup_ifname(ifcfg, &iface))
    {
        LOGE("BSAL Failed to lookup radio with ifname: %s", ifcfg->ifname);
        goto error;
    }

    if (!group_add_iface(&iface)) goto error;

    if (is_group_initialized())
    {
        if (!create_wifihal_ap_config_list(&wifihal_cfg)) goto error;

        int ret = wifi_steering_setGroup(group.index, group.iface_number, wifihal_cfg);
        free(wifihal_cfg);
        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to add radio group #%u"
                 " (wifi_steering_setGroup() failed with code %d)",
                 group.index,
                 ret);
            goto error;
        }

        LOGI("BSAL Ifaces added to group #%u", group.index);
    }
    else
    {
        LOGI("BSAL Postpone ifaces group addition until all radios are set");
    }

    return 0;

error:
    return -1;
}

int target_bsal_iface_update(const bsal_ifconfig_t *ifcfg)
{
    iface_t iface;
    wifi_steering_apConfig_t *wifihal_cfg;

    if (!lookup_ifname(ifcfg, &iface))
    {
        LOGE("BSAL Failed to lookup radio with ifname: %s", ifcfg->ifname);
        goto error;
    }

    if (!group_update_iface(&iface)) goto error;

    if (is_group_initialized())
    {
        if (!create_wifihal_ap_config_list(&wifihal_cfg)) goto error;

        int ret = wifi_steering_setGroup(group.index, group.iface_number, wifihal_cfg);
        free(wifihal_cfg);
        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to update radio group #%u"
                 " (wifi_steering_setGroup() failed with code %d)",
                 group.index,
                 ret);
            goto error;
        }

        LOGI("BSAL Updated group #%u",
             group.index);
    }
    else
    {
        LOGI("BSAL Postpone ifaces update until both radios are set");
    }

    return 0;

error:
    return -1;
}

int target_bsal_iface_remove(const bsal_ifconfig_t *ifcfg)
{
    iface_t iface;

    if (!lookup_ifname(ifcfg, &iface))
    {
        LOGE("BSAL Failed to lookup radio with ifname: %s", ifcfg->ifname);
        goto error;
    }

    if (!group_remove_iface(&iface)) goto error;

    if (is_group_uninitialized())
    {
        int ret = wifi_steering_setGroup(group.index, 0, NULL);
        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to remove radio group #%u (wifi_steering_setGroup() failed with code %d)",
                 group.index, ret);
            goto error;
        }

        LOGI("BSAL Removed group #%u", group.index);
    }
    else
    {
        LOGI("BSAL Postpone iface removal until both radios are removed");
    }

    return 0;

error:
    return -1;
}

/* This function is a workaround for inconstent / not documented API
 * usage. The core's BM expects that rssiProbeHWM and rssiProbeLWM
 * set to "zero" means "no blocking for that client". The wifihal
 * API doesn't specify such special case.
 * We set the HWM to very high value here to effectively get the
 * same functionality (allow all probe ant auth frames for that
 * client).
 * SNR 120 is so high that all frames in real life will match and at the same
 * time it doesn't exceed INT8_MAX (127) if by any chance the value is casted to
 * a signed char/int8_t in the HAL implementation.
 */
static void
bsal_convert_if_not_blocking(wifi_steering_clientConfig_t *client)
{
    if (client->rssiProbeHWM == 0 && client->rssiProbeLWM == 0) {
        client->rssiProbeHWM = 120;
        client->rssiAuthHWM = 120;
        LOGI("BSAL converting from <0,0> LWM HWM. rssiProbeHWM = %u, rssiAuthHWM = %u",
             client->rssiProbeHWM, client->rssiAuthHWM);
    }
}

int target_bsal_client_add(
        const char *ifname,
        const uint8_t *mac_addr,
        const bsal_client_config_t *cfg)
{
    const iface_t *iface = NULL;
    wifi_steering_clientConfig_t wifihal_cfg;
    int ret = 0;

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to add client "MAC_ADDR_FMT" (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    bsal_client_to_wifi_steering_client(cfg, &wifihal_cfg);

    bsal_convert_if_not_blocking(&wifihal_cfg);

    ret = wifi_steering_clientSet(group.index, iface->wifihal_cfg.apIndex, (UCHAR*) mac_addr, &wifihal_cfg);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to add client "MAC_ADDR_FMT" to iface: %s (wifi_steering_clientSet() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Client "MAC_ADDR_FMT" added to iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);

    return 0;

error:
    return -1;
}

int target_bsal_client_update(
        const char *ifname,
        const uint8_t *mac_addr,
        const bsal_client_config_t *cfg)
{
    const iface_t *iface = NULL;
    wifi_steering_clientConfig_t wifihal_cfg;
    int ret = 0;

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to update client "MAC_ADDR_FMT" (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    bsal_client_to_wifi_steering_client(cfg, &wifihal_cfg);

    bsal_convert_if_not_blocking(&wifihal_cfg);

    ret = wifi_steering_clientSet(group.index, iface->wifihal_cfg.apIndex, (UCHAR*) mac_addr, &wifihal_cfg);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to update client "MAC_ADDR_FMT" to iface: %s (wifi_steering_clientSet() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Client "MAC_ADDR_FMT" updated on iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);

    return 0;

error:
    return -1;
}

int target_bsal_client_remove(
        const char *ifname,
        const uint8_t *mac_addr)
{
    const iface_t *iface = NULL;
    int ret = 0;

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to remove client "MAC_ADDR_FMT" (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    ret = wifi_steering_clientRemove(group.index, iface->wifihal_cfg.apIndex, (UCHAR*) mac_addr);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to remove client "MAC_ADDR_FMT" to iface: %s (wifi_steering_clientRemove() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Client "MAC_ADDR_FMT" removed from iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);

    return 0;

error:
    return -1;
}

int target_bsal_client_disconnect(
        const char *ifname,
        const uint8_t *mac_addr,
        bsal_disc_type_t type,
        uint8_t reason)
{
    const iface_t *iface = NULL;
    int ret = 0;
    wifi_disconnectType_t disc_type = DISCONNECT_TYPE_DEAUTH;

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to disconnect client "MAC_ADDR_FMT" (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    switch (type)
    {
        case BSAL_DISC_TYPE_DISASSOC:
            disc_type = DISCONNECT_TYPE_DISASSOC;
            break;

        case BSAL_DISC_TYPE_DEAUTH:
            disc_type = DISCONNECT_TYPE_DEAUTH;
            break;

        default:
            return -1;
    }

    ret = wifi_steering_clientDisconnect(group.index, iface->wifihal_cfg.apIndex, (UCHAR*) mac_addr, disc_type, reason);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed disconnect client "MAC_ADDR_FMT" to iface: %s (wifi_steering_clientDisconnect() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Disconnected client  "MAC_ADDR_FMT" on iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);

    return 0;

error:
    return -1;
}

static bool bsal_client_set_connected(
        INT apIndex,
        const uint8_t *mac_addr,
        bsal_client_info_t *info)
{
    wifi_associated_dev3_t *clients = NULL;
    UINT clients_num = 0;
    UINT i = 0;
    int wifi_hal_ret = 0;
#ifndef CONFIG_RDK_HAS_ASSOC_REQ_IES
    bsal_client_info_cache_t *client_info_cache;
#endif

    wifi_hal_ret = wifi_getApAssociatedDeviceDiagnosticResult3(apIndex, &clients, &clients_num);
    if (wifi_hal_ret != RETURN_OK)
    {
        LOGE("BSAL Failed to fetch clients associated with iface: %d (wifi_getApAssociatedDeviceDiagnosticResult3() "
             "failed with code %d)", apIndex, wifi_hal_ret);
        return false;
    }

    LOGI("BSAL Found %u clients associated with iface: %d", clients_num, apIndex);

    memset(info, 0, sizeof(*info));
    for (i = 0; i < clients_num; i++)
    {
        if (memcmp(mac_addr, &clients[i].cli_MACAddress, sizeof(clients[i].cli_MACAddress)) == 0)
        {
            info->connected = true;
            info->snr = clients[i].cli_SNR;
            info->tx_bytes = clients[i].cli_BytesSent;
            info->rx_bytes = clients[i].cli_BytesReceived;
#ifndef CONFIG_RDK_HAS_ASSOC_REQ_IES
            client_info_cache = bsal_find_client_info(mac_addr);
            if (client_info_cache != NULL)
            {
                client_info_cache->client.connected = true;
                client_info_cache->client.snr = clients[i].cli_SNR;
                client_info_cache->client.rx_bytes = clients[i].cli_BytesReceived;
                client_info_cache->client.tx_bytes = clients[i].cli_BytesSent;
                memcpy(info, &client_info_cache->client, sizeof(*info));
            }
#endif
            LOGI("BSAL Client "MAC_ADDR_FMT" is connected apIndex: %d, SNR: %d, rx: %lld, tx: %lld", MAC_ADDR_UNPACK(mac_addr),
                apIndex, info->snr, info->rx_bytes, info->tx_bytes);

            break;
        }
    }

    free(clients);
    return true;
}

int target_bsal_bss_tm_request(
        const char *ifname,
        const uint8_t *mac_addr,
        const bsal_btm_params_t *btm_params)
{
    const iface_t *iface = NULL;
    wifi_BTMRequest_t req;
    unsigned int bss_term_flag = 0;
    int i = 0;
    int ret = 0;
    mac_address_t mac = {0};

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to prepare BTM request for client "MAC_ADDR_FMT" is connected (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    memset(&req, 0, sizeof(req));

    if (btm_params->bss_term == true)
    {
        bss_term_flag = 1;
        req.termDuration.tsf = 0;  // imminently
        req.termDuration.duration = btm_params->bss_term;
    }

    req.token = 0x10;  // any
    req.requestMode = BTM_DEFAULT_PREF | btm_params->abridged << 1 | btm_params->disassoc_imminent << 2 | bss_term_flag << 3;
    req.validityInterval = btm_params->valid_int;
    req.numCandidates = btm_params->num_neigh;

    assert(btm_params->num_neigh < MAX_CANDIDATES);

    // Build neighbor list
    for (i = 0; i < btm_params->num_neigh; i++)
    {
        const bsal_neigh_info_t *neigh = &btm_params->neigh[i];

        memcpy(&req.candidates[i].bssid, &neigh->bssid, sizeof(neigh->bssid));

        // Reachability 1: A station sending a pre-authentication frame to the BSSID will not receive a response
        req.candidates[i].info = 0x01;
        req.candidates[i].opClass = neigh->op_class;
        req.candidates[i].channel = neigh->channel;
        req.candidates[i].phyTable = neigh->phy_type;

        // Preference: Ordering of preferences for the BSS transition candidates for this STA
        req.candidates[i].bssTransitionCandidatePreference.preference = 0x01;
    }

    memcpy(mac, mac_addr, sizeof(mac));
    ret = wifi_setBTMRequest(iface->wifihal_cfg.apIndex, mac, &req);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to send BTM request to client "MAC_ADDR_FMT" on iface: %s (wifi_setBTMRequest() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Sent BTM request to client "MAC_ADDR_FMT" on iface: %s", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname);

    return 0;

error:
    return -1;
}

int target_bsal_rrm_beacon_report_request(
        const char *ifname,
        const uint8_t *mac_addr,
        const bsal_rrm_params_t *rrm_params)
{
    const iface_t *iface = NULL;
    wifi_BeaconRequest_t req;
    unsigned char dia_token = 0;
    int ret = 0;
    mac_address_t mac = {0};

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to prepare RRM request for client "MAC_ADDR_FMT" is connected (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    memset(&req, 0, sizeof(req));
    req.opClass = rrm_params->op_class;
    req.mode = rrm_params->meas_mode;
    req.channel = rrm_params->channel;
    req.randomizationInterval = rrm_params->rand_ivl;
    req.numRepetitions = 1;
    req.duration = rrm_params->meas_dur;

    if (rrm_params->req_ssid == 1)
    {
        if (wifi_getSSIDName(iface->wifihal_cfg.apIndex, req.ssid) == RETURN_OK)
        {
            req.ssidPresent = 1;
        }
        else
        {
            req.ssidPresent = 0;
            LOGW("BSAL unable to get SSID for iface: %s. Sent RRM request with wildcarded SSID", ifname);
        }
    }
    memset(req.bssid, 0xFF, sizeof(req.bssid));

    LOGD("BSAL RRM Request opClass: %d, mode: %d, channel: %d, randomizationInterval: %d, numRepetitions: %d, "
                          "duration: %d, ssidPresent: %d, bssid: "MAC_ADDR_FMT", ssid: %s",
                          req.opClass, req.mode, req.channel, req.randomizationInterval, req.numRepetitions,
                          req.duration, req.ssidPresent, MAC_ADDR_UNPACK(req.bssid), req.ssid);

    memcpy(mac, mac_addr, sizeof(mac));
    ret = wifi_setRMBeaconRequest(iface->wifihal_cfg.apIndex, mac, &req, &dia_token);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to send RRM request to client "MAC_ADDR_FMT" on iface: %s (wifi_setRMBeaconRequest() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Sent RRM request to client "MAC_ADDR_FMT" on iface: %s", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname);

    return 0;

error:
    return -1;
}

#ifdef CONFIG_RDK_HAS_ASSOC_REQ_IES
static int get_ht_mcs_max(uint32_t mcs_set)
{
    int i;

    LOGD("HT mcs_set = %x\n", mcs_set);
    // We don't handle mcs_set == 0. Just return 0 in that case.
    for (i = 0; i < 32; i++)
    {
        // Shift right the mcs_set until no more bits
        // are set. Amount of the shifts equals to
        // the position of highest bit set to '1'. Position
        // of highest '1' determines the max supported
        // MCS. We check here only for 0-31 MCS set.
        mcs_set = mcs_set >> 1;
        if (mcs_set == 0) break;
    }

    return i;
}

static int get_vht_mcs_max(uint16_t rx_mcs_map)
{
    int i;
    int max_mcs = 0;

    for (i = 0; i < 8; i++)
    {
        switch (rx_mcs_map & 0x03)
        {
            case 0x00:
                max_mcs = max_mcs < 7 ? 7 : max_mcs;
                break;
            case 0x01:
                max_mcs = max_mcs < 8 ? 8 : max_mcs;
                break;
            case 0x02:
                max_mcs = max_mcs < 9 ? 9 : max_mcs;
                break;
            default:
                // Not supported or invalid
                break;
        }

        rx_mcs_map = rx_mcs_map >> 2;
    }

    return max_mcs;
}

static int get_vht_nss_max(uint16_t rx_mcs_map)
{
    int i;
    int number_of_spatial_streams = 0;

    for (i = 0; i < 8; i++)
    {
        // Set number of spatial streams for highest found valid bit pair.
        if ((rx_mcs_map & 0x03) != 0x03) number_of_spatial_streams = i + 1;
        rx_mcs_map = rx_mcs_map >> 2;
    }

    return number_of_spatial_streams;
}

static void bm_parse_btm_supported(bsal_client_info_t *info, uint32_t ext_caps)
{
    // We only check the first 4 bytes from extended capabilities
    info->is_BTM_supported = !!(ext_caps & IEEE80211_EXTCAPIE_BSSTRANSITION);
}

static void bm_parse_rrm_supported(bsal_client_info_t *info, uint8_t rm_cap_oct1,
                                uint8_t rm_cap_oct2, uint8_t rm_cap_oct5)
{
    info->is_RRM_supported = true;
    info->rrm_caps.link_meas = !!(rm_cap_oct1 & IEEE80211_RRM_CAPS_LINK_MEASUREMENT);
    info->rrm_caps.neigh_rpt = !!(rm_cap_oct1 & IEEE80211_RRM_CAPS_NEIGHBOR_REPORT);
    info->rrm_caps.bcn_rpt_passive = !!(rm_cap_oct1 & IEEE80211_RRM_CAPS_BEACON_REPORT_PASSIVE);
    info->rrm_caps.bcn_rpt_active = !!(rm_cap_oct1 & IEEE80211_RRM_CAPS_BEACON_REPORT_ACTIVE);
    info->rrm_caps.bcn_rpt_table = !!(rm_cap_oct1 & IEEE80211_RRM_CAPS_BEACON_REPORT_TABLE);
    info->rrm_caps.lci_meas = !!(rm_cap_oct2 & IEEE80211_RRM_CAPS_LCI_MEASUREMENT);
    info->rrm_caps.ftm_range_rpt = !!(rm_cap_oct5 & IEEE80211_RRM_CAPS_FTM_RANGE_REPORT);
}

static void bm_parse_ht_cap(bsal_client_info_t *info, uint16_t ht_cap_info, uint32_t mcs_set)
{
    int ht_mcs_max;
    int ht_nss_max;

    if ((ht_cap_info & IEEE80211_HTCAP_C_CHWIDTH40) && (info->datarate_info.max_chwidth == 0))
    {
        info->datarate_info.max_chwidth = 1;  // 40 MHz
    }

    ht_mcs_max = get_ht_mcs_max(mcs_set);
    ht_nss_max = ht_mcs_max / 8 + 1;

    if (info->datarate_info.max_MCS < ht_mcs_max)
    {
        info->datarate_info.max_MCS = ht_mcs_max % 8;  // we always normalize to VHT
    }

    if (info->datarate_info.max_streams < ht_nss_max)
    {
        info->datarate_info.max_streams = ht_nss_max;
    }

    // If SMPS is set to 0 it means "Capable of SM Power Save (Static)"
    info->datarate_info.is_static_smps =
        (ht_cap_info & IEEE80211_HTCAP_C_SM_MASK) == 0x00 ? 1 : 0;
}

static void bm_parse_pwr_cap(bsal_client_info_t *info, uint8_t max_tx_power)
{
    info->datarate_info.max_txpower = max_tx_power;
}

static void bm_parse_vht_cap(bsal_client_info_t *info, uint32_t vht_info, uint16_t rx_mcs_map)
{
    int vht_max;
    int nss_max;

    info->datarate_info.max_chwidth = 2;  // 80 MHz
    if (vht_info & IEEE80211_VHTCAP_SHORTGI_160)
    {
        info->datarate_info.max_chwidth = 3;  // 160 MHz
    }

    vht_max = get_vht_mcs_max(rx_mcs_map);
    nss_max = get_vht_nss_max(rx_mcs_map);

    if (info->datarate_info.max_MCS < vht_max)
    {
        info->datarate_info.max_MCS = vht_max;
    }
    if (info->datarate_info.max_streams < nss_max)
    {
        info->datarate_info.max_streams = nss_max;
    }

    info->datarate_info.is_mu_mimo_supported = !!(vht_info & IEEE80211_VHTCAP_MU_BFORMEE);
}

static void set_client_legacy(bsal_client_info_t *info)
{
    // The 2.4/5G capabilities are tracked by BM based on probe request.
    // Don't set it here. The phy_mode is also not set, as it can be calculated
    // by the cloud if max BW, MCS and NSS are provided.
    // Besides that, assume it is a legacy client. If capabilities are discovered,
    // they will overwrite the below defaults. The NSS is set to '0' for legacy clients.
    info->is_BTM_supported = 0;
    info->is_RRM_supported = 0;
    memset(&info->datarate_info, 0, sizeof(info->datarate_info));
    memset(&info->rrm_caps, 0, sizeof(info->rrm_caps));
}
#endif

int target_bsal_client_info(
        const char *ifname,
        const uint8_t *mac_addr,
        bsal_client_info_t *info)
{
    const iface_t *iface = NULL;
    INT apIndex;
#ifdef CONFIG_RDK_HAS_ASSOC_REQ_IES
    const struct element *elem;
    INT ret;
    CHAR req_ies[1024];
    UINT req_ies_len;
#endif

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to check client "MAC_ADDR_FMT" info (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        return -1;
    }

    apIndex = iface->wifihal_cfg.apIndex;

    memset(info, 0, sizeof(*info));

    if (!bsal_client_set_connected(apIndex, mac_addr, info))
    {
        return -1;
    }

#ifdef CONFIG_RDK_HAS_ASSOC_REQ_IES
    if (!info->connected) return 0;

    memset(req_ies, 0, sizeof(req_ies));
    ret = wifi_getAssociationReqIEs(apIndex, (const mac_address_t *)mac_addr,
            req_ies, sizeof(req_ies), &req_ies_len);
    if (ret != RETURN_OK)
    {
        LOGE("Cannot get association request IEs for MAC %02x:%02x:%02x:%02x:%02x:%02x on apIndex = %d",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], apIndex);
        return -1;
    }

    if (req_ies_len > sizeof(info->assoc_ies))
    {
        LOGE("%s: The IEs are too big: %d (max: %d)",
             __func__, req_ies_len, (int)sizeof(info->assoc_ies));
        return -1;
    }

    memcpy(info->assoc_ies, req_ies, req_ies_len);
    info->assoc_ies_len = req_ies_len;

    set_client_legacy(info);

    for_each_element(elem, req_ies, req_ies_len) {
        switch (elem->id) {
            case WLAN_EID_EXT_CAPAB:
                bm_parse_btm_supported(info, le32toh(*(uint32_t *)elem->data));
                break;
            case WLAN_EID_RRM_ENABLED_CAPABILITIES:
                bm_parse_rrm_supported(info, elem->data[0], elem->data[1], elem->data[4]);
                break;
            case WLAN_EID_HT_CAP:
                bm_parse_ht_cap(info, le16toh(*(uint16_t *)elem->data),
                    le32toh(*(uint32_t *)&elem->data[3]));
                break;
            case WLAN_EID_VHT_CAP:
                bm_parse_vht_cap(info, le32toh(*(uint32_t *)elem->data), le16toh(*(uint16_t*)&elem->data[4]));
                break;
            case WLAN_EID_PWR_CAPABILITY:
                bm_parse_pwr_cap(info, elem->data[1]);
                break;
            default:
                break;
        }
    }
#endif

    return 0;
}

static int bssid_ifname_cmp(void *_a, void *_b)
{
    bsal_neigh_key_t *a = _a;
    bsal_neigh_key_t *b = _b;
    int rc;

    rc = memcmp(&a->bssid, &b->bssid, sizeof(a->bssid));
    if (rc != 0) return rc;

    rc = memcmp(&a->ifname, &b->ifname, sizeof(a->ifname));
    if (rc != 0) return rc;

    return 0;
}

static int bsal_send_update_neighbor_list(const char *ifname)
{
    bsal_neighbor_t         *iface_neighbor;
    UINT                    iface_neighbors_number = 0;
    wifi_NeighborReport_t   *neighbor_reports = NULL;
    INT                     ap_index;
    uint32_t                i = 0;
    INT                     ret;

    ds_tree_foreach(&bsal_ifaces_neighbors, iface_neighbor)
    {
        if (!strcmp(iface_neighbor->key.ifname, ifname))
        {
            iface_neighbors_number++;
        }
    }

    if (!vif_ifname_to_idx(ifname, &ap_index))
    {
        return -1;
    }

    if (iface_neighbors_number > 0)
    {
        neighbor_reports = calloc(iface_neighbors_number, sizeof(wifi_NeighborReport_t));
        if (!neighbor_reports)
        {
            LOGE("%s:%d: unable to allocate memory", __func__, __LINE__);
            return -1;
        }

        // fill in the wifi_hal list with neighbors
        ds_tree_foreach(&bsal_ifaces_neighbors, iface_neighbor)
        {
            if (strcmp(iface_neighbor->key.ifname, ifname))
            {
                continue;
            }
            if (i >= iface_neighbors_number)
            {
                break;
            }
            memcpy(neighbor_reports[i].bssid, iface_neighbor->nr.bssid, sizeof(neighbor_reports[i].bssid));
            neighbor_reports[i].info = iface_neighbor->nr.bssid_info;
            neighbor_reports[i].opClass = iface_neighbor->nr.op_class;
            neighbor_reports[i].channel = iface_neighbor->nr.channel;
            neighbor_reports[i].phyTable = iface_neighbor->nr.phy_type;
            i++;
        }
    }

    ret =  wifi_setNeighborReports((UINT)ap_index, iface_neighbors_number, neighbor_reports);
    if (neighbor_reports) free(neighbor_reports);

    if (ret != RETURN_OK)
    {
        LOGE("%s: unable to setNeighborReports for %s", __func__, ifname);
        return -1;
    }

    return 0;
}

int target_bsal_rrm_set_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    bsal_neighbor_t     *iface_neighbor;
    bsal_neigh_key_t    key;
    int                 ret;

    memset(&key, 0, sizeof(key));
    memcpy(key.bssid, nr->bssid, sizeof(key.bssid));
    STRSCPY(key.ifname, ifname);

    // Insert or modify neighbor in the list
    iface_neighbor = ds_tree_find(&bsal_ifaces_neighbors, &key);
    if (iface_neighbor)
    {
        memcpy(&iface_neighbor->nr, nr, sizeof(iface_neighbor->nr));
    }
    else
    {
        iface_neighbor = calloc(1, sizeof(bsal_neighbor_t));
        if (!iface_neighbor)
        {
            LOGE("%s:%d: unable to allocate memory", __func__, __LINE__);
            return -1;
        }
        memcpy(&iface_neighbor->nr, nr, sizeof(iface_neighbor->nr));
        memcpy(&iface_neighbor->key, &key, sizeof(iface_neighbor->key));
        ds_tree_insert(&bsal_ifaces_neighbors, iface_neighbor, &iface_neighbor->key);
    }

    LOGD("BSAL: %s: inserted neighbor %s, "MAC_ADDR_FMT, __func__, ifname, MAC_ADDR_UNPACK(nr->bssid));

    ret = bsal_send_update_neighbor_list(ifname);
    if (ret)
    {
        ds_tree_remove(&bsal_ifaces_neighbors, iface_neighbor);
        free(iface_neighbor);

        LOGE("BSAL: %s: failed adding neighbor %s, "MAC_ADDR_FMT, __func__, ifname, MAC_ADDR_UNPACK(nr->bssid));
        return ret;
    }

    LOGI("BSAL: %s: succesfully added neighbor %s, "MAC_ADDR_FMT, __func__, ifname, MAC_ADDR_UNPACK(nr->bssid));

    return 0;
}

int target_bsal_rrm_remove_neighbor(const char *ifname, const bsal_neigh_info_t *nr)
{
    bsal_neighbor_t     *iface_neighbor;
    bsal_neigh_key_t    key;
    int                 ret;

    memset(&key, 0, sizeof(key));
    memcpy(key.bssid, nr->bssid, sizeof(key.bssid));
    STRSCPY(key.ifname, ifname);

    iface_neighbor = ds_tree_find(&bsal_ifaces_neighbors, &key);
    if (!iface_neighbor)
    {
        LOGD("%s: unable to find neighbor bssid="MAC_ADDR_FMT" ifaname=%s", __func__,
                                                    MAC_ADDR_UNPACK(nr->bssid), ifname);
        return 0;
    }

    ds_tree_remove(&bsal_ifaces_neighbors, iface_neighbor);

    ret = bsal_send_update_neighbor_list(ifname);
    if (ret)
    {
        ds_tree_insert(&bsal_ifaces_neighbors, iface_neighbor, &key);
        LOGE("BSAL: %s: failed removing neighbor %s, "MAC_ADDR_FMT, __func__, ifname, MAC_ADDR_UNPACK(nr->bssid));
        return ret;
    }
    free(iface_neighbor);

    LOGI("BSAL: %s: succesfully removed neighbor %s, "MAC_ADDR_FMT, __func__, ifname, MAC_ADDR_UNPACK(nr->bssid));

    return 0;
}

