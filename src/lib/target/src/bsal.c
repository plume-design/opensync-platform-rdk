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

/*****************************************************************************/

#define WIFI_HAL_STR_LEN  64
#define BTM_DEFAULT_PREF 1
#define MAC_STR_LEN       18

#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"

#define MAC_ADDR_UNPACK(addr) addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

typedef struct
{
    bsal_band_t band;
    bsal_ifconfig_t bsal_cfg;
    wifi_steering_apConfig_t wifihal_cfg;
} iface_t;

typedef struct
{
    UINT index;
    iface_t *iface_24g;
    iface_t *iface_5g;

    // Private fields
    iface_t _iface_24g;
    iface_t _iface_5g;
} bsal_group_t;

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

/*****************************************************************************/

static void process_event(
        UINT steeringgroupIndex,
        wifi_steering_event_t *wifi_hal_event)
{
    bsal_event_t *bsal_event = NULL;
    bsal_band_t band = BSAL_BAND_24G;
    uint32_t val = 0;

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

    if (group.iface_24g && group.iface_24g->wifihal_cfg.apIndex == wifi_hal_event->apIndex)
    {
        band = BSAL_BAND_24G;
        STRSCPY(bsal_event->ifname, group.iface_24g->bsal_cfg.ifname);
    }
    else if (group.iface_5g && group.iface_5g->wifihal_cfg.apIndex == wifi_hal_event->apIndex)
    {
        band = BSAL_BAND_5G;
        STRSCPY(bsal_event->ifname, group.iface_5g->bsal_cfg.ifname);
    }
    else
    {
        LOGD("BSAL Dropping event received for unknown iface (apIndex: %d)", wifi_hal_event->apIndex);
        goto end;
    }

    bsal_event->band = band;

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

        bsal_event->data.connect.is_BTM_supported = wifi_hal_event->data.connect.isBTMSupported;
        bsal_event->data.connect.is_RRM_supported = wifi_hal_event->data.connect.isRRMSupported;

        bsal_event->data.connect.band_cap_2G = wifi_hal_event->data.connect.bandCap2G;
        bsal_event->data.connect.band_cap_5G = wifi_hal_event->data.connect.bandCap5G;

        bsal_event->data.connect.datarate_info.max_chwidth = wifi_hal_event->data.connect.datarateInfo.maxChwidth;
        bsal_event->data.connect.datarate_info.max_streams = wifi_hal_event->data.connect.datarateInfo.maxStreams;
        bsal_event->data.connect.datarate_info.phy_mode = wifi_hal_event->data.connect.datarateInfo.phyMode;
        bsal_event->data.connect.datarate_info.max_MCS = wifi_hal_event->data.connect.datarateInfo.maxMCS;
        bsal_event->data.connect.datarate_info.max_txpower = wifi_hal_event->data.connect.datarateInfo.maxTxpower;
        bsal_event->data.connect.datarate_info.is_static_smps = wifi_hal_event->data.connect.datarateInfo.isStaticSmps;
        bsal_event->data.connect.datarate_info.is_mu_mimo_supported = wifi_hal_event->data.connect.datarateInfo.isMUMimoSupported;

        bsal_event->data.connect.rrm_caps.link_meas = wifi_hal_event->data.connect.rrmCaps.linkMeas;
        bsal_event->data.connect.rrm_caps.neigh_rpt = wifi_hal_event->data.connect.rrmCaps.neighRpt;
        bsal_event->data.connect.rrm_caps.bcn_rpt_passive = wifi_hal_event->data.connect.rrmCaps.bcnRptPassive;
        bsal_event->data.connect.rrm_caps.bcn_rpt_active = wifi_hal_event->data.connect.rrmCaps.bcnRptActive;
        bsal_event->data.connect.rrm_caps.bcn_rpt_table = wifi_hal_event->data.connect.rrmCaps.bcnRptTable;
        bsal_event->data.connect.rrm_caps.lci_meas = wifi_hal_event->data.connect.rrmCaps.lciMeas;
        bsal_event->data.connect.rrm_caps.ftm_range_rpt = wifi_hal_event->data.connect.rrmCaps.ftmRangeRpt;
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
        memset(buf, 0, sizeof(buf));
        ret = wifi_getApName(s, buf);
        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to get ifname of VAO #%u (wifi_getApName() failed with code %d)",
                 s, ret);
            goto error;
        }

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
    ret = wifi_getRadioOperatingFrequencyBand(radio_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to get operating freq of radio #%d "
             "(wifi_getRadioOperatingFrequencyBand() failed with code %d)",
             radio_index, ret);
        goto error;
    }

    if (buf[0] == '2')
    {
        iface->band = BSAL_BAND_24G;
    }
    else if (buf[0] == '5')
    {
        iface->band = BSAL_BAND_5G;
    }
    else
    {
        LOGE("BSAL Radio #%d has unknown operating freq %s", radio_index, buf);
        goto error;
    }

    memcpy(&iface->bsal_cfg, ifcfg, sizeof(iface->bsal_cfg));

    iface->wifihal_cfg.apIndex = s;
    iface->wifihal_cfg.utilCheckIntervalSec = iface->bsal_cfg.chan_util_check_sec;
    iface->wifihal_cfg.utilAvgCount = iface->bsal_cfg.chan_util_avg_count;
    iface->wifihal_cfg.inactCheckIntervalSec = iface->bsal_cfg.inact_check_sec;
    iface->wifihal_cfg.inactCheckThresholdSec = iface->bsal_cfg.inact_tmout_sec_normal;

    LOGI("BSAL Found apIndex #%d (ifname: %s, band: %s)", iface->wifihal_cfg.apIndex,
         iface->bsal_cfg.ifname, iface->band == BSAL_BAND_24G ? "2.4G" : "5G");

    return true;

error:
    return false;
}

static iface_t* group_get_iface_by_name(const char *ifname)
{
    if (group.iface_24g && (strcmp(ifname, group.iface_24g->bsal_cfg.ifname) == 0))
    {
        return group.iface_24g;
    }

    if (group.iface_5g && (strcmp(ifname, group.iface_5g->bsal_cfg.ifname) == 0))
    {
        return group.iface_5g;
    }

    return NULL;
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

int target_bsal_init(
        bsal_event_cb_t event_cb,
        struct ev_loop *loop)
{
    int ret;

    if (_bsal_event_cb != NULL)
    {
        LOGW("BSAL callback already initialized");
        goto error;
    }

    _bsal_event_cb = event_cb;
    memset(&group, 0, sizeof(group));

    // Register the callback
    ret = wifi_steering_eventRegister(process_event);
    if (ret < 0)
    {
        LOGE("BSAL Event callback registration failed (wifi_steering_eventRegister() "
             "failed with code %d)", ret);
        goto error;
    }

    LOGI("BSAL initialized");

    return 0;

error:
    return -1;
}

int target_bsal_cleanup(void)
{
    wifi_steering_eventUnregister();

    _bsal_event_cb = NULL;

    LOGI("BSAL cleaned up");

    return 0;
}

int target_bsal_iface_add(const bsal_ifconfig_t *ifcfg)
{
    iface_t iface;

    if (!lookup_ifname(ifcfg, &iface))
    {
        LOGE("BSAL Failed to lookup radio with ifname: %s", ifcfg->ifname);
        goto error;
    }

    if (iface.band == BSAL_BAND_24G)
    {
        if (group.iface_24g != NULL)
        {
            LOGE("BSAL 2.4G iface: %s is already configured", group.iface_24g->bsal_cfg.ifname);
            goto error;
        }

        memcpy(&group._iface_24g, &iface, sizeof(group._iface_24g));
        group.iface_24g = &group._iface_24g;
    }

    if (iface.band == BSAL_BAND_5G)
    {
        if (group.iface_5g != NULL)
        {
            LOGE("BSAL 5G iface: %s is already configured", group.iface_5g->bsal_cfg.ifname);
            goto error;
        }

        memcpy(&group._iface_5g, &iface, sizeof(group._iface_5g));
        group.iface_5g = &group._iface_5g;
    }

    if ((group.iface_24g != NULL) && (group.iface_5g != NULL))
    {
        int ret = wifi_steering_setGroup(group.index,
                                         &group.iface_24g->wifihal_cfg,
                                         &group.iface_5g->wifihal_cfg);

        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to add radio group #%u of ifaces: %s and %s "
                 " (wifi_steering_setGroup() failed with code %d)",
                 group.index,
                 group.iface_24g->bsal_cfg.ifname,
                 group.iface_5g->bsal_cfg.ifname,
                 ret);
            goto error;
        }

        LOGI("BSAL Added group #%u of ifaces: %s and %s",
             group.index,
             group.iface_24g->bsal_cfg.ifname,
             group.iface_5g->bsal_cfg.ifname);
    }
    else
    {
        LOGI("BSAL Postpone ifaces group addition until both radios are set (24G: %s; 5G: %s)",
             group.iface_24g ? group.iface_24g->bsal_cfg.ifname : "N/A",
             group.iface_5g ? group.iface_5g->bsal_cfg.ifname : "N/A");
    }

    return 0;

error:
    return -1;
}

int target_bsal_iface_update(const bsal_ifconfig_t *ifcfg)
{
    iface_t iface;

    if (!lookup_ifname(ifcfg, &iface))
    {
        LOGE("BSAL Failed to lookup radio with ifname: %s", ifcfg->ifname);
        goto error;
    }

    if (iface.band == BSAL_BAND_24G)
    {
        if (group.iface_24g == NULL)
        {
            LOGE("BSAL 2.4G iface is not configured");
            goto error;
        }

        memcpy(&group._iface_24g, &iface, sizeof(group._iface_24g));
        group.iface_24g = &group._iface_24g;
    }

    if (iface.band == BSAL_BAND_5G)
    {
        if (group.iface_5g == NULL)
        {
            LOGE("BSAL 5G iface is not already configured");
            goto error;
        }

        memcpy(&group._iface_5g, &iface, sizeof(group._iface_5g));
        group.iface_5g = &group._iface_5g;
    }

    if ((group.iface_24g != NULL) && (group.iface_5g != NULL))
    {
        // Unfortunately, the underlying setGroup implementation can fail
        // if both configs are already set. Because there is no WifiHAL
        // "update" equivalent, we first delete the group, and then
        // add it back with new configs.
        int ret = wifi_steering_setGroup(group.index, NULL, NULL);

        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to remove radio group #%u during update "
                 "(wifi_steering_setGroup() failed with code %d)",
                 group.index,
                 ret);
            goto error;
        }

        ret = wifi_steering_setGroup(group.index,
                                     &group.iface_24g->wifihal_cfg,
                                     &group.iface_5g->wifihal_cfg);

        if (ret != RETURN_OK)
        {
            LOGE("BSAL Failed to update radio group #%u of ifaces: %s and %s "
                 " (wifi_steering_setGroup() failed with code %d)",
                 group.index,
                 group.iface_24g->bsal_cfg.ifname,
                 group.iface_5g->bsal_cfg.ifname,
                 ret);
            goto error;
        }

        LOGI("BSAL Updated group #%u of ifaces: %s and %s",
             group.index,
             group.iface_24g->bsal_cfg.ifname,
             group.iface_5g->bsal_cfg.ifname);
    }
    else
    {
        LOGI("BSAL Postpone ifaces update until both radios are set (24G: %s; 5G: %s)",
             group.iface_24g ? group.iface_24g->bsal_cfg.ifname : "NULL",
             group.iface_5g ? group.iface_5g->bsal_cfg.ifname : "NULL");
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

    if (iface.band == BSAL_BAND_24G)
    {
        if (group.iface_24g == NULL)
        {
            LOGE("BSAL 2.4G iface is not configured");
            goto error;
        }

        group.iface_24g = NULL;
    }

    if (iface.band == BSAL_BAND_5G)
    {
        if (group.iface_5g == NULL)
        {
            LOGE("BSAL 5G iface is not already configured");
            goto error;
        }

        group.iface_5g = NULL;
    }

    if ((group.iface_24g == NULL) && (group.iface_5g == NULL))
    {
        int ret = wifi_steering_setGroup(group.index, NULL, NULL);

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
        LOGI("BSAL Postpone iface removal until both radios are set (24G: %s; 5G: %s)",
             group.iface_24g ? group.iface_24g->bsal_cfg.ifname : "NULL",
             group.iface_5g ? group.iface_5g->bsal_cfg.ifname : "NULL");
    }

    return 0;

error:
    return -1;
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

int target_bsal_client_measure(
        const char *ifname,
        const uint8_t *mac_addr,
        int num_samples)
{
    const iface_t *iface = NULL;
    int ret = 0;

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable to trigger measurement for client "MAC_ADDR_FMT" (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        goto error;
    }

    ret = wifi_steering_clientMeasure(group.index, iface->wifihal_cfg.apIndex, (UCHAR*) mac_addr);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to trigger measurement for client "MAC_ADDR_FMT" to iface: %s (wifi_steering_clientMeasure() "
             "failed with code %d)", MAC_ADDR_UNPACK(mac_addr), iface->bsal_cfg.ifname, ret);
        goto error;
    }

    LOGI("BSAL Triggered measurement for client "MAC_ADDR_FMT" on iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);

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

int target_bsal_client_is_connected(
        const char *ifname,
        const uint8_t *mac_addr)
{
    const iface_t *iface = NULL;
    wifi_associated_dev2_t *clients = NULL;
    UINT clients_num = 0;
    UINT i = 0;
    int ret = 0;
    int result = 0;  // Not connected

    iface = group_get_iface_by_name(ifname);
    if (iface == NULL)
    {
        LOGE("BSAL Unable check if client "MAC_ADDR_FMT" is connected (failed to find iface: %s)",
             MAC_ADDR_UNPACK(mac_addr), ifname);
        result = -1;
        goto end;
    }

    ret = wifi_getApAssociatedDeviceDiagnosticResult2(iface->wifihal_cfg.apIndex, &clients, &clients_num);
    if (ret != RETURN_OK)
    {
        LOGE("BSAL Failed to fetch clients associated with iface: %s (wifi_getApAssociatedDeviceDiagnosticResult2() "
             "failed with code %d)", iface->bsal_cfg.ifname, ret);
        result = -1;
        goto end;
    }

    LOGI("BSAL Found %u clients associated with iface: %s", clients_num, iface->bsal_cfg.ifname);

    for (i = 0; i < clients_num; i++)
    {
        if (memcmp(mac_addr, &clients[i].cli_MACAddress, sizeof(clients[i].cli_MACAddress)) == 0)
        {
            LOGI("BSAL Client is "MAC_ADDR_FMT" is associated with iface: %s", MAC_ADDR_UNPACK(mac_addr), ifname);
            result = 1;
            break;
        }
    }

end:
    free(clients);
    return result;
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

    ret = wifi_setBTMRequest(iface->wifihal_cfg.apIndex, (CHAR*) mac_addr, &req);
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
    /*req.numRepetitions = 1;*/  // This field is not available in wifi_BeaconRequest_t
    req.duration = rrm_params->meas_dur;

    if (rrm_params->req_ssid != 2)
    {
        LOGW( "BSAL Incorrect req_ssid: %d, expecting: 2", rrm_params->req_ssid);
        goto error;
    }

    req.ssidPresent = 0;
    memset(req.bssid, 0xFF, sizeof(req.bssid));

    ret = wifi_setRMBeaconRequest(iface->wifihal_cfg.apIndex, (CHAR*) mac_addr, &req, &dia_token);
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

int target_bsal_client_info(
        const char *ifname,
        const uint8_t *mac_addr,
        bsal_client_info_t *info)
{
    wifi_associated_dev3_t  *associated_dev = NULL;
    UINT                     num_devices = 0;
    INT                      ret;
    INT                      apIndex;
    ULONG                    i;
    char                     mac[MAC_STR_LEN];
    os_macaddr_t             macaddr;
    CHAR                     ifname_temp[BSAL_IFNAME_LEN];

    memset(ifname_temp, 0, sizeof(ifname_temp));
    // RDK Wifi HAL discards the 'const', so the copy is to avoid warnings
    STRSCPY(ifname_temp, ifname);

    ret = wifi_getApIndexFromName(ifname_temp, &apIndex);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Cannot get Ap Index", ifname);
        return -1;
    }

    ret = wifi_getApAssociatedDeviceDiagnosticResult3(apIndex, &associated_dev, &num_devices);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to fetch associated devices", ifname);
        return -1;
    }
    LOGD("%s: Found %u existing associated clients %d", ifname, num_devices, apIndex);

    for (i = 0; i < num_devices; i++)
    {
        ret = memcmp(mac_addr, associated_dev[i].cli_MACAddress, BSAL_MAC_ADDR_LEN);
        if (ret == 0)
        {
            memcpy(&macaddr, associated_dev[i].cli_MACAddress, sizeof(macaddr));
            snprintf(mac, sizeof(mac), PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));
            LOGD("%s: Client %s is connected", ifname_temp, mac);
            info->connected = true;
            goto exit;
        }
    }
    info->connected = false;

exit:
    free(associated_dev);
    return 0;
}
