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
 * bsal_wifihal.c
 *
 * Band Steering Abstraction Layer API
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>

#define _LINUX_IF_H  /* Avoid redefinition of stuff */
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>

#include "log.h"
#include "const.h"
#include "os_nif.h"
#include "ds_tree.h"
#include "bsal.h"

#include "wifihal.h"

/*****************************************************************************/

#define MODULE_ID               LOG_MODULE_ID_BSAL

#define MAX_AP_INDEX 16

#define BSAL_HANDLE(x)          (bsal_t *)x
#define BSAL_PAIR(x)            (bsal_pair_t *)x

/*****************************************************************************/

typedef struct
{
    bool                enabled;
    int                 steering_group_index;

    int32_t             ifcfg_24_ap_index;
    bsal_ifconfig_t     ifcfg_24;

    int32_t             ifcfg_5_ap_index;
    bsal_ifconfig_t     ifcfg_5;

    ds_tree_node_t      dst_node;
} bsal_pair_t;

/*****************************************************************************/

static bsal_event_cb_t _bsal_event_cb = NULL;

static ds_tree_t _bsal_pairs = DS_TREE_INIT((ds_key_cmp_t *)strcmp,
                                            bsal_pair_t,
                                            dst_node);

static c_item_t map_disc_source[] = {
    C_ITEM_VAL(DISCONNECT_SOURCE_LOCAL,             BSAL_DISC_SOURCE_LOCAL ),
    C_ITEM_VAL(DISCONNECT_SOURCE_REMOTE,            BSAL_DISC_SOURCE_REMOTE)
};

static c_item_t map_disc_type[] = {
    C_ITEM_VAL(DISCONNECT_TYPE_DISASSOC,            BSAL_DISC_TYPE_DISASSOC),
    C_ITEM_VAL(DISCONNECT_TYPE_DEAUTH,              BSAL_DISC_TYPE_DEAUTH  )
};

static c_item_t map_rssi_xing[] = {
    C_ITEM_VAL(WIFI_STEERING_RSSI_UNCHANGED,        BSAL_RSSI_UNCHANGED    ),
    C_ITEM_VAL(WIFI_STEERING_RSSI_LOWER,            BSAL_RSSI_LOWER        ),
    C_ITEM_VAL(WIFI_STEERING_RSSI_HIGHER,           BSAL_RSSI_HIGHER       )
};

/*****************************************************************************/

extern INT wifi_getAssociatedDeviceDetail(
        INT apIndex,
        INT devIndex,
        wifi_device_t *output_struct);

/*****************************************************************************/

static bool
bsal_ifcfg_to_wifi_steering_apconf(
        bsal_ifconfig_t *bsal_ifcfg,
        wifi_steering_apConfig_t *ap_conf)
{
    ap_conf->utilCheckIntervalSec   = bsal_ifcfg->chan_util_check_sec;
    ap_conf->utilAvgCount           = bsal_ifcfg->chan_util_avg_count;

    ap_conf->inactCheckIntervalSec  = bsal_ifcfg->inact_check_sec;
    ap_conf->inactCheckThresholdSec = bsal_ifcfg->inact_tmout_sec_normal;

    return true;
}

static bool
bsal_client_to_wifi_steering_client(
        bsal_client_config_t *bsal_client,
        wifi_steering_clientConfig_t *wifi_client)
{

    wifi_client->rssiProbeHWM       = bsal_client->rssi_probe_hwm;
    wifi_client->rssiProbeLWM       = bsal_client->rssi_probe_lwm;

    wifi_client->rssiAuthHWM        = bsal_client->rssi_auth_hwm;
    wifi_client->rssiAuthLWM        = bsal_client->rssi_auth_lwm;

    wifi_client->rssiInactXing      = bsal_client->rssi_inact_xing;
    wifi_client->rssiHighXing       = bsal_client->rssi_high_xing;
    wifi_client->rssiLowXing        = bsal_client->rssi_low_xing;

    wifi_client->authRejectReason   = bsal_client->auth_reject_reason;

    return true;
}

static bsal_pair_t *
bsal_pair_by_apIndex(int32_t apIndex)
{
    bsal_pair_t     *pair;

    ds_tree_foreach(&_bsal_pairs, pair)
    {
        if (   pair->ifcfg_24_ap_index == apIndex
            || pair->ifcfg_5_ap_index  == apIndex)
        {
            return pair;
        }
    }

    return NULL;
}

static int
bsal_ifname_to_apIndex(char *ifname)
{
    char    ap_name[64];
    int     i;

    for (i = 0; i < MAX_AP_INDEX; i++)
    {
        if (wifi_getApName(i, ap_name) < 0) {
            LOGE("bsal_ifname_to_apIndex: failed to get ifname for index %d\n", i);
            return -1;
        }

        if (!strcmp(ap_name, ifname)) {
            return i;
        }
    }

    return -1;
}

// NB: Ifnames are usually ath0/ath1, ath6/ath7 etc.
//     The digit associated with the 2.4 Ghz ifname of a pair is halved and
//     used as the the pair's steering group index. Hence, ath0/ath1 will
//     group 0, ath6/ath7 will be (6/2) == 3, and so on.
static bool
bsal_get_steering_group_index(char *ifname, int *steering_group_index)
{
    int val = -1;

    if (!ifname) {
        LOGW("get_steering_group_index: passed ifname is NULL");
        return NULL;
    }

    val = bsal_ifname_to_apIndex(ifname);
    if (val < 0) {
        LOGE("Failed to get index for ifname '%s'", ifname);
        return false;
    }

    *steering_group_index = val / 2;
    LOGD("Steering group index of ifname '%s' is '%d'",
         ifname, *steering_group_index);

    return true;
}

static bool
bsal_get_apIndex(char *ifname, int *apIndex)
{
    ds_dlist_t          *wifihal_radios = NULL;
    wifihal_radio_t     *radio          = NULL;
    wifihal_ssid_t      *ssid           = NULL;

    if (strlen(ifname) == 0) {
        return false;
    }

    wifihal_radios = wifihal_get_radios();
    if (!wifihal_radios) {
        LOGE("apIndex by band: Unable to get wifihal_radios");
        return false;
    }

    ds_dlist_foreach(wifihal_radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            if (!strcmp(ssid->ifname, ifname)) {
                *apIndex = ssid->index;
                return true;
            }
        }
    }

    return false;
}

static bsal_ifconfig_t *
bsal_ifcfg_by_band(bsal_pair_t *pair, bsal_band_t band)
{
    switch (band)
    {
    case BSAL_BAND_24G:
        return &pair->ifcfg_24;

    case BSAL_BAND_5G:
        return &pair->ifcfg_5;

    default:
        break;
    }

    return NULL;
}

static bool
bsal_band_by_apIndex(bsal_pair_t *pair, int apIndex, bsal_band_t *band)
{
    if (apIndex == pair->ifcfg_24_ap_index)
    {
        *band = BSAL_BAND_24G;
        return true;
    }
    else if (apIndex == pair->ifcfg_5_ap_index)
    {
        *band = BSAL_BAND_5G;
        return true;
    }

    return true;
}

static bool
bsal_apIndex_by_band(bsal_pair_t *pair, bsal_band_t band, int *apIndex)
{
    switch (band)
    {
        case BSAL_BAND_24G:
            *apIndex = pair->ifcfg_24_ap_index;
            return true;

        case BSAL_BAND_5G:
            *apIndex = pair->ifcfg_5_ap_index;
            return true;

        default:
            return false;
    }

    return false;
}

/*****************************************************************************/

void
bsal_wifihal_event_process(UINT steeringgroupIndex, wifi_steering_event_t *wifi_hal_event)
{
    bsal_event_t                    *bsal_event;
    bsal_band_t                     band;
    uint32_t                        val;

    bsal_pair_t                     *pair = NULL;

    // If we don't have a callback, just ignore the data
    if (!_bsal_event_cb) {
        return;
    }

    if (!(pair = bsal_pair_by_apIndex(wifi_hal_event->apIndex))) {
        //LOGD("bsal_wifihal_event_process: failed to find bsal for apIndex '%d'",
        //     wifi_hal_event->apIndex);
        return;
    }

    if (bsal_band_by_apIndex(pair, wifi_hal_event->apIndex, &band) == false) {
        LOGE("Failed to find band by apIndex %u", wifi_hal_event->apIndex);
        return;
    }

    if (!(bsal_event = (bsal_event_t *)calloc(1, sizeof(*bsal_event)))) {
        LOGE("Failed to allocate memory for new event!");
        return;
    }
    bsal_event->band = band;

    switch (wifi_hal_event->type)
    {
    case WIFI_STEERING_EVENT_PROBE_REQ:
        bsal_event->type = BSAL_EVENT_PROBE_REQ;
        memcpy(&bsal_event->data.probe_req.client_addr,
                                        &wifi_hal_event->data.probeReq.client_mac,
                                        sizeof(bsal_event->data.probe_req.client_addr));
        bsal_event->data.probe_req.rssi      = wifi_hal_event->data.probeReq.rssi;
        bsal_event->data.probe_req.ssid_null = wifi_hal_event->data.probeReq.broadcast ? true : false;
        bsal_event->data.probe_req.blocked   = wifi_hal_event->data.probeReq.blocked   ? true : false;
        break;

    case WIFI_STEERING_EVENT_AUTH_FAIL:
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
        bsal_event->data.auth_fail.rssi          = wifi_hal_event->data.authFail.rssi;
        bsal_event->data.auth_fail.reason        = wifi_hal_event->data.authFail.reason;
        bsal_event->data.auth_fail.bs_blocked    = wifi_hal_event->data.authFail.bsBlocked;
        bsal_event->data.auth_fail.bs_rejected   = wifi_hal_event->data.authFail.bsRejected;
        break;

    case WIFI_STEERING_EVENT_CLIENT_CONNECT:
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
        bsal_event->data.connect.datarate_info.phy_mode    = wifi_hal_event->data.connect.datarateInfo.phyMode;
        bsal_event->data.connect.datarate_info.max_MCS     = wifi_hal_event->data.connect.datarateInfo.maxMCS;
        bsal_event->data.connect.datarate_info.max_txpower = wifi_hal_event->data.connect.datarateInfo.maxTxpower;
        bsal_event->data.connect.datarate_info.is_static_smps = wifi_hal_event->data.connect.datarateInfo.isStaticSmps;
        bsal_event->data.connect.datarate_info.is_mu_mimo_supported = wifi_hal_event->data.connect.datarateInfo.isMUMimoSupported;

        bsal_event->data.connect.rrm_caps.link_meas = wifi_hal_event->data.connect.rrmCaps.linkMeas;
        bsal_event->data.connect.rrm_caps.neigh_rpt = wifi_hal_event->data.connect.rrmCaps.neighRpt;
        bsal_event->data.connect.rrm_caps.bcn_rpt_passive = wifi_hal_event->data.connect.rrmCaps.bcnRptPassive;
        bsal_event->data.connect.rrm_caps.bcn_rpt_active  = wifi_hal_event->data.connect.rrmCaps.bcnRptActive;
        bsal_event->data.connect.rrm_caps.bcn_rpt_table   = wifi_hal_event->data.connect.rrmCaps.bcnRptTable;
        bsal_event->data.connect.rrm_caps.lci_meas        = wifi_hal_event->data.connect.rrmCaps.lciMeas;
        bsal_event->data.connect.rrm_caps.ftm_range_rpt   = wifi_hal_event->data.connect.rrmCaps.ftmRangeRpt;

        break;

    case WIFI_STEERING_EVENT_CLIENT_DISCONNECT:
        bsal_event->type = BSAL_EVENT_CLIENT_DISCONNECT;
        memcpy(&bsal_event->data.disconnect.client_addr,
                                        &wifi_hal_event->data.disconnect.client_mac,
                                        sizeof(bsal_event->data.disconnect.client_addr));

        if (!c_get_value_by_key(map_disc_source, wifi_hal_event->data.disconnect.source, &val)) {
            LOGE("bsal_wifihal_event_process(WIFI_STEERING_EVENT_CLIENT_DISCONNECT): "
                 "Unknown source %d", wifi_hal_event->data.disconnect.source);
            free(bsal_event);
            return;
        }
        bsal_event->data.disconnect.source = val;

        if (!c_get_value_by_key(map_disc_type, wifi_hal_event->data.disconnect.type, &val)) {
            LOGE("bsal_wifihal_event_process(WIFI_STEERING_EVENT_CLIENT_DISCONNECT): "
                 "Unknown type %d", wifi_hal_event->data.disconnect.type);
            free(bsal_event);
            return;
        }
        bsal_event->data.disconnect.type = val;

        bsal_event->data.disconnect.reason = wifi_hal_event->data.disconnect.reason;
        break;

    case WIFI_STEERING_EVENT_CLIENT_ACTIVITY:
        bsal_event->type = BSAL_EVENT_CLIENT_ACTIVITY;
        memcpy(&bsal_event->data.activity.client_addr,
                                        &wifi_hal_event->data.activity.client_mac,
                                        sizeof(bsal_event->data.activity.client_addr));
        bsal_event->data.activity.active = wifi_hal_event->data.activity.active ? true : false;
        break;

    case WIFI_STEERING_EVENT_CHAN_UTILIZATION:
        bsal_event->type = BSAL_EVENT_CHAN_UTILIZATION;
        bsal_event->data.chan_util.utilization = wifi_hal_event->data.chanUtil.utilization;
        break;

    case WIFI_STEERING_EVENT_RSSI_XING:
        bsal_event->type = BSAL_EVENT_RSSI_XING;
        memcpy(&bsal_event->data.rssi_change.client_addr,
                                        &wifi_hal_event->data.rssiXing.client_mac,
                                        sizeof(bsal_event->data.rssi_change.client_addr));
        bsal_event->data.rssi_change.rssi = wifi_hal_event->data.rssiXing.rssi;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.inactveXing, &val)) {
            LOGE("bsal_wifihal_event_process(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown inact %d", wifi_hal_event->data.rssiXing.inactveXing);
            free(bsal_event);
            return;
        }
        bsal_event->data.rssi_change.inact_xing = val;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.highXing, &val)) {
            LOGE("bsal_wifihal_event_process(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown high %d", wifi_hal_event->data.rssiXing.highXing);
            free(bsal_event);
            return;
        }
        bsal_event->data.rssi_change.high_xing = val;

        if (!c_get_value_by_key(map_rssi_xing, wifi_hal_event->data.rssiXing.lowXing, &val)) {
            LOGE("bsal_wifihal_event_process(WIFI_STEERING_EVENT_RSSI_CROSSING): "
                 "Unknown low %d", wifi_hal_event->data.rssiXing.lowXing);
            free(bsal_event);
            return;
        }
        bsal_event->data.rssi_change.low_xing = val;
        break;

    case WIFI_STEERING_EVENT_RSSI:
        bsal_event->type = BSAL_EVENT_RSSI;
        memcpy(&bsal_event->data.rssi.client_addr,
                                        &wifi_hal_event->data.rssi.client_mac,
                                        sizeof(bsal_event->data.rssi.client_addr));
        bsal_event->data.rssi.rssi = wifi_hal_event->data.rssi.rssi;
        break;

    default:
        // ignore this event
        free(bsal_event);
        return;
    }

    _bsal_event_cb(BSAL_HANDLE(pair), bsal_event);

    // Free the memory allocated for event here, as _bsal_event_cb will
    // allocate the memory and copy the contents
    if (bsal_event) {
        free(bsal_event);
    }

    return;
}

bsal_t
bsal_ifpair_add(bsal_ifconfig_t *ifcfg_24, bsal_ifconfig_t *ifcfg_5)
{
    int                         ret;
    int                         apIndex;
    bsal_pair_t                 *pair = NULL;
    int                         steering_group_index;

    wifi_steering_apConfig_t    cfg_2;
    wifi_steering_apConfig_t    cfg_5;

    if (!ifcfg_24 || !ifcfg_5) {
        return NULL;
    }

    pair = calloc(1, sizeof(*pair));
    if (!pair) {
        LOGE("Failed to allocate memory for a bsal pair, errno = %d,(%s)",
             errno, strerror(errno));
        return NULL;
    }

    memset(&cfg_2, 0, sizeof(cfg_2));
    memset(&cfg_5, 0, sizeof(cfg_5));

    memcpy(&pair->ifcfg_24, ifcfg_24, sizeof(pair->ifcfg_24));
    memcpy(&pair->ifcfg_5,  ifcfg_5,  sizeof(pair->ifcfg_5 ));

    if (!bsal_get_apIndex(ifcfg_24->ifname, &apIndex)) {
        LOGW("ifpair add: unable to get apIndex for ifname '%s'", ifcfg_24->ifname);
        goto exit;
    }
    pair->ifcfg_24_ap_index = apIndex;

    if (!bsal_get_steering_group_index(ifcfg_24->ifname, &steering_group_index)) {
        LOGW("Unable to get steering group index for ifname '%s'", ifcfg_24->ifname);
        goto exit;
    }

    pair->steering_group_index = steering_group_index;

    if (!bsal_get_apIndex(ifcfg_5->ifname, &apIndex)) {
        LOGW("ifpair add: unable to get apIndex for ifname '%s'", ifcfg_5->ifname);
        goto exit;
    }
    pair->ifcfg_5_ap_index = apIndex;

    bsal_ifcfg_to_wifi_steering_apconf(ifcfg_24, &cfg_2);
    bsal_ifcfg_to_wifi_steering_apconf(ifcfg_5 , &cfg_5);

    cfg_2.apIndex = pair->ifcfg_24_ap_index;
    cfg_5.apIndex = pair->ifcfg_5_ap_index;

    ret = wifi_steering_setGroup(pair->steering_group_index, &cfg_2, &cfg_5);
    if (ret < 0) {
        LOGE("ifpair add: wifi_steering_setGroup() failed");
        return NULL;
    }

    pair->enabled = true;
    ds_tree_insert(&_bsal_pairs, pair, pair->ifcfg_24.ifname);

    LOGD("Initialized if-pair %s/%s with index %d",
         pair->ifcfg_24.ifname, pair->ifcfg_5.ifname, pair->steering_group_index);

    return BSAL_HANDLE(pair);

exit:
    if (pair) {
        free(pair);
    }

    return NULL;
}

int
bsal_ifpair_update(
        bsal_t bsal,
        bsal_band_t band,
        bsal_ifconfig_t *ifcfg)
{
    bsal_ifconfig_t             *pair_cfg;
    bsal_pair_t                 *pair = BSAL_PAIR(bsal);
    wifi_steering_apConfig_t    apConfig;

    int                         apIndex;
    int                         ret;

    if (!pair->enabled) {
        LOGW("ifpair update: BSAL pair with index '%d' is not enabled",
             pair->steering_group_index);
        return -1;
    }

    if (!(pair_cfg = bsal_ifcfg_by_band(pair, band))) {
        LOGW("ifpair update: invalid band %d", band);
        return -1;
    }

    // Cannot change interface name
    if (strncmp(ifcfg->ifname, pair_cfg->ifname, sizeof(pair_cfg->ifname)) != 0) {
        LOGW("ifconfig update: cannot change interface name (%s -> %s)",
             pair_cfg->ifname, ifcfg->ifname);
        return -1;
    }

    bsal_ifcfg_to_wifi_steering_apconf(ifcfg, &apConfig);

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("ifpair update: unable to get apIndex for band %d", band);
        return -1;
    }
    apConfig.apIndex = apIndex;

    if (band == BSAL_BAND_24G)
    {
        ret = wifi_steering_setGroup(pair->steering_group_index, &apConfig, NULL);
        if (ret < 0)
        {
            LOGE("ifpair update 2.4GHz: wifi_steering_setGroup() failed, reapplying old config");

            // Re-apply old apConfig
            bsal_ifcfg_to_wifi_steering_apconf(pair_cfg, &apConfig);
            wifi_steering_setGroup(pair->steering_group_index, &apConfig, NULL);

            return -1;
        }
    }
    else if (band == BSAL_BAND_5G)
    {
        ret = wifi_steering_setGroup(pair->steering_group_index, NULL, &apConfig);
        if (ret < 0)
        {
            LOGE("ifpair update 5GHz: wifi_steering_setGroup() failed, reapplying old config");

            // Re-apply old apConfig
            bsal_ifcfg_to_wifi_steering_apconf(pair_cfg, &apConfig);
            wifi_steering_setGroup(pair->steering_group_index, NULL, &apConfig);

            return -1;
        }
    }

    memcpy(pair_cfg, ifcfg, sizeof(*pair_cfg));

    return 0;
}

int
bsal_ifpair_remove(bsal_t bsal)
{
    int             ret;
    bsal_pair_t     *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        return -1;
    }

    ret = wifi_steering_setGroup(pair->steering_group_index, NULL, NULL);
    if (ret < 0) {
        LOGE("BSAL ifpair remove: wifi_steering_setGroup failed");
        return -1;
    }

    pair->enabled = false;

    ds_tree_remove(&_bsal_pairs, pair);
    free(pair);

    return 0;
}

int
bsal_client_add(
        bsal_t  bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        bsal_client_config_t *conf)
{
    int                             apIndex;
    wifi_steering_clientConfig_t    client;
    bsal_pair_t                     *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        LOGW("client_add: BSAL pair '%d' is disabled", pair->steering_group_index);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client_add: unable to get apIndex for band %d", band);
        return -1;
    }

    bsal_client_to_wifi_steering_client(conf, &client);

    return wifi_steering_clientSet(pair->steering_group_index, apIndex, mac_addr, &client);
}

int
bsal_client_update(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        bsal_client_config_t *conf)
{
    int                             apIndex;
    wifi_steering_clientConfig_t    client;
    bsal_pair_t                     *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        LOGW("client_update: BSAL pair '%d' is disabled", pair->steering_group_index);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client_update: unable to get apIndex for band %d", band);
        return -1;
    }

    bsal_client_to_wifi_steering_client(conf, &client);

    return wifi_steering_clientSet(pair->steering_group_index, apIndex, mac_addr, &client);
}

int
bsal_client_remove(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr)
{
    int             apIndex;
    bsal_pair_t     *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        LOGW("client remove: BSAL pair '%d' is disabled", pair->steering_group_index);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client remove: unable to get apIndex for band %d", band);
        return -1;
    }

    return wifi_steering_clientRemove(pair->steering_group_index, apIndex, mac_addr);
}

int
bsal_client_measure(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        int num_samples)
{
    int             apIndex;
    bsal_pair_t     *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        LOGW("client_measure: BSAL pair '%d' is disabled", pair->enabled);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client remove: unable to get apIndex for band %d", band);
        return -1;
    }

    return wifi_steering_clientMeasure(pair->steering_group_index, apIndex, mac_addr);
}

int
bsal_client_disconnect(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        bsal_disc_type_t type, uint8_t reason)
{
    int                     ret;
    int                     apIndex;
    wifi_disconnectType_t   disc_type;
    bsal_pair_t             *pair = BSAL_PAIR(bsal);

    if (!pair->enabled) {
        LOGW("client_disconnect: BSAL pair '%d' is disabled",
             pair->steering_group_index);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client_disconnect: unable to get apIndex for band %d", band);
        return -1;
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

    ret = wifi_steering_clientDisconnect(
            pair->steering_group_index,
            apIndex,
            mac_addr,
            disc_type, reason);

    if (ret < 0) {
        LOGW("wifi_steering_clientDisconnect failed");
        return -1;
    }

    return 0;
}

int
bsal_client_is_connected(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr)
{
    bsal_pair_t     *pair = BSAL_PAIR(bsal);
    int             apIndex, ret;
    wifi_device_t   wd;
    os_macaddr_t    macaddr;
    unsigned long   num_devices = 0;
    unsigned long   i;
    char            mac[20];

    if (!pair->enabled) {
        LOGW("client_is_connected: BSAL pair '%d' is disabled",
             pair->steering_group_index);
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("client remove: unable to get apIndex for band %d", band);
        return -1;
    }

    WIFIHAL_TM_START();
    ret = wifi_getApNumDevicesAssociated(apIndex, &num_devices);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApNumDevicesAssociated(%d) ret %ld",
                                         apIndex, num_devices);
    if (ret != RETURN_OK) {
        LOGE("%s: Failed to fetch number of associated devices",
             (band == BSAL_BAND_5G) ? "5GHz" : "2.4GHz");
        return 0;
    }

    LOGD("%s: Found %ld existing associated devices",
         (band == BSAL_BAND_5G) ? "5GHz" :"2.4GHz", num_devices);

    for (i = 0; i < num_devices; i++)
    {
        memset(&wd, 0, sizeof(wd));

        WIFIHAL_TM_START();
        ret = wifi_getAssociatedDeviceDetail(apIndex, (i+1), &wd);
        if (ret != RETURN_OK)
        {
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getAssociatedDeviceDetail(%d, %ld)",
                                             apIndex, (i+1));
            LOGE("%s: Failed to get details for associated dev #%ld",
                 (band == BSAL_BAND_5G) ? "5GHz" : "2.4GHz", i);
            continue;
        }

        memcpy(&macaddr, wd.wifi_devMacAddress, sizeof(macaddr));
        snprintf(mac, sizeof(mac)-1, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, macaddr));
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getAssociatedDeviceDetail(%d, %ld) ret \"%s\"",
                                             apIndex, (i+1), (ret == RETURN_OK) ? mac : "");

        if (memcmp(mac_addr, (uint8_t *)&macaddr, sizeof(*mac_addr)) == 0)
        {
            LOGI("Client '%s' is connected on %s band", mac,
                 (band == BSAL_BAND_5G) ? "5GHz" : "2.4GHz");
            return 1;
        }
    }

    return 0;
}

int
bsal_bss_tm_request(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        target_bsal_btm_params_t *btm_params)
{
    bsal_pair_t                 *pair = BSAL_PAIR(bsal);
    int                         apIndex, i, ret;
    char                        hostapd_cmd[2048]   = { 0 };
    char                        btm_req_cmd[1024]   = { 0 };
    char                        neigh_list[512]     = { 0 };
    char                        cmd[128]            = { 0 };

    target_bsal_neigh_info_t    *neigh              = NULL;

    os_macaddr_t                temp;
    char                        mac_str[18]         = { 0 };
    char                        client_mac[18]      = { 0 };
    char                        ifname[17]          = { 0 };

    if (!pair->enabled) {
        LOGW("BSS TM Request: BSAL pair not enabled");
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("BSS TM Request: unable to get apIndex for band %d", band);
        return -1;
    }

    switch (band)
    {
        case BSAL_BAND_24G:
            memcpy(ifname, pair->ifcfg_24.ifname, sizeof(ifname));
            break;

        case BSAL_BAND_5G:
            memcpy(ifname, pair->ifcfg_5.ifname, sizeof(ifname));
            break;

        default:
            LOGW("bss_tm_req: Invalid band '%d'", band);
            return -1;
    }

    // hostapd_cli expects client mac address in ASCII format
    memset(&temp, 0, sizeof(temp));
    memcpy(&temp, mac_addr, sizeof(temp));
    sprintf(client_mac, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, temp));

    // Build neighbor list
    for (i = 0; i < btm_params->num_neigh; i++)
    {
        neigh = &btm_params->neigh[i];

        memset(&mac_str, 0, sizeof(mac_str));
        memset(&cmd,     0, sizeof(cmd    ));
        memset(&temp,    0, sizeof(temp   ));

        memcpy(&temp, neigh->bssid, sizeof(temp));
        sprintf(mac_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, temp));

        snprintf(cmd, sizeof(cmd),
                 "neighbor=%s,%u,%hhu,%hhu,%hhu ",
                 mac_str, neigh->bssid_info, neigh->op_class,
                 neigh->channel, neigh->phy_type);

        strcat(neigh_list, cmd);
    }

    // Build the hostapd bss_tm_req command
    snprintf(btm_req_cmd, sizeof(btm_req_cmd),
             "%s %s valid_int=%hhu pref=%hhu abridged=%hhu disassoc_imminent=%hhu",
             client_mac, (strlen(neigh_list) ? neigh_list : ""),
             btm_params->valid_int, btm_params->pref, btm_params->abridged,
             btm_params->disassoc_imminent);

    LOGD("Client %s - hostapd_cli bss_tm_req command: %s", client_mac, btm_req_cmd);

    snprintf(hostapd_cmd, sizeof(hostapd_cmd),
             "hostapd_cli -i %s bss_tm_req %s", ifname, btm_req_cmd);

    ret = system(hostapd_cmd);
    if (WEXITSTATUS(ret) != 0) {
        LOGE("hostapd_cli bss_tm_req failed, '%s' (rc = %d)\n",
             hostapd_cmd, WEXITSTATUS(ret));
        return -1;
    }

    return 0;
}

int
bsal_rrm_beacon_report_request(
        bsal_t bsal,
        bsal_band_t band,
        uint8_t *mac_addr,
        target_bsal_rrm_params_t *rrm_params)
{
    bsal_pair_t     *pair = BSAL_PAIR(bsal);
    int             apIndex, ret;
    os_macaddr_t    temp;
    char            rrm_bcn_rpt_cmd[1024]   = { 0 };
    char            client_mac[18]          = { 0 };
    char            ifname[17]              = { 0 };
    char            wifitool_cmd[2048]      = { 0 };

    if (!pair->enabled) {
        LOGW("BSAL pair not enabled");
        return -1;
    }

    if (!bsal_apIndex_by_band(pair, band, &apIndex)) {
        LOGW("RRM Bcn Report Request: unable to get apIndex for band %d", band);
        return -1;
    }

    switch (band)
    {
        case BSAL_BAND_24G:
            memcpy(ifname, pair->ifcfg_24.ifname, sizeof(ifname));
            break;

        case BSAL_BAND_5G:
            memcpy(ifname, pair->ifcfg_5.ifname, sizeof(ifname));
            break;

        default:
            LOGW("rrm_beacon_report_request: Invalid band '%d'", band);
            return -1;
    }

    // hostapd_cli expects client mac address in ASCII format
    memset(&temp, 0, sizeof(temp));
    memcpy(&temp, mac_addr, sizeof(temp));
    sprintf(client_mac, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, temp));

    // Build the wifitool bcnrpt command
    snprintf(rrm_bcn_rpt_cmd, sizeof(rrm_bcn_rpt_cmd),
             "%s %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu",
             client_mac, rrm_params->op_class, rrm_params->channel,
             rrm_params->rand_ivl, rrm_params->meas_dur, rrm_params->meas_mode,
             rrm_params->req_ssid, rrm_params->rep_cond, rrm_params->rpt_detail,
             rrm_params->req_ie, rrm_params->chanrpt_mode);

    LOGD("Client %s - rrm_bcn_rpt request command: %s", client_mac, rrm_bcn_rpt_cmd);

    snprintf(wifitool_cmd, sizeof(wifitool_cmd),
             "wifitool %s sendbcnrpt %s", ifname, rrm_bcn_rpt_cmd);

    ret = system(wifitool_cmd);
    if (WEXITSTATUS(ret) != 0) {
        LOGE("wifitool sendbcnrpt failed, '%s' (rc = %d)\n",
             wifitool_cmd, WEXITSTATUS(ret));
        return -1;
    }

    return 0;
}

/*****************************************************************************/

int
bsal_init(bsal_event_cb_t event_cb)
{
    int         ret;

    if (_bsal_event_cb) {
        LOGW("BSAL cb already initialized");
        return -1;
    }

    _bsal_event_cb = event_cb;

    // Register the callback
    ret = wifi_steering_eventRegister(bsal_wifihal_event_process);
    if (ret < 0) {
        LOGE("Event cb registration failed");
        return -1;
    }

    LOGI("BSAL initialized");

    return 0;
}

int
bsal_event_cleanup(void)
{
    ds_tree_iter_t      iter;
    bsal_pair_t         *pair;

    LOGI("BSAL cleaning up");

    wifi_steering_eventUnregister();

    pair = ds_tree_ifirst(&iter, &_bsal_pairs);
    while (pair)
    {
        ds_tree_iremove(&iter);

        if (pair) {
            free(pair);
        }

        pair = ds_tree_inext(&iter);
    }

    _bsal_event_cb     = NULL;

    return 0;
}
