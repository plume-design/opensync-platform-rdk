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
#include <errno.h>

#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "target.h"
#include "const.h"
#include "wifihal.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_HAL

#define WIFIHAL_SCAN_MAX_RECORDS     300

/*****************************************************************************/

static struct {
    char                *phymode;
    radio_chanwidth_t   chanwidth;
}
g_phymode_bw_table[] =
{
    { "11A",             RADIO_CHAN_WIDTH_20MHZ},
    { "11B",             RADIO_CHAN_WIDTH_20MHZ},
    { "11G",             RADIO_CHAN_WIDTH_20MHZ},
    { "11NA_HT20",       RADIO_CHAN_WIDTH_20MHZ},
    { "11NG_HT20",       RADIO_CHAN_WIDTH_20MHZ},
    { "11NA_HT40PLUS",   RADIO_CHAN_WIDTH_40MHZ_ABOVE},
    { "11NA_HT40MINUS",  RADIO_CHAN_WIDTH_40MHZ_BELOW},
    { "11NG_HT40PLUS",   RADIO_CHAN_WIDTH_40MHZ_ABOVE},
    { "11NG_HT40MINUS",  RADIO_CHAN_WIDTH_40MHZ_BELOW},
    { "11NG_HT40",       RADIO_CHAN_WIDTH_40MHZ},
    { "11NA_HT40",       RADIO_CHAN_WIDTH_40MHZ},
    { "11AC_VHT20",      RADIO_CHAN_WIDTH_20MHZ},
    { "11AC_VHT40PLUS",  RADIO_CHAN_WIDTH_40MHZ_ABOVE},
    { "11AC_VHT40MINUS", RADIO_CHAN_WIDTH_40MHZ_BELOW},
    { "11AC_VHT40",      RADIO_CHAN_WIDTH_40MHZ},
    { "11AC_VHT80",      RADIO_CHAN_WIDTH_80MHZ},
#ifdef QTN_WIFI
    // Values QTN_WIFI is currently returning
    { "80",              RADIO_CHAN_WIDTH_80MHZ},
    { "40",              RADIO_CHAN_WIDTH_40MHZ},
    { "20",              RADIO_CHAN_WIDTH_20MHZ},
#endif /* QTN_WIFI */
};

/*****************************************************************************/

static radio_chanwidth_t phymode_to_chanwidth(char *phymode)
{
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(g_phymode_bw_table); i++) {
        if (strcmp(phymode, g_phymode_bw_table[i].phymode) == 0) {
            return g_phymode_bw_table[i].chanwidth;
        }
    }

    // for unknown return 20MHz
    return RADIO_CHAN_WIDTH_20MHZ;
}


int wifihal_mcs_nss_bw_to_dpp_index(int mcs, int nss, int bw)
{
    if (!nss) return mcs;

    return
        WIFIHAL_STATS_LEGACY_RECORDS
        + mcs
        + (nss-1) * WIFIHAL_STATS_MSC_QTY
        + bw * WIFIHAL_STATS_MSC_QTY * WIFIHAL_STATS_NSS_QTY;
}

static bool radio_entry_to_hal_radio_index(radio_entry_t *radio_cfg, int *radioIndex)
{
    wifihal_radio_t *radio;

    if (!(radio = wifihal_radio_by_ifname(radio_cfg->phy_name)))
    {
        LOGW("%s: radio not found", radio_cfg->phy_name);
        return false;
    }
    *radioIndex = radio->index;

    return true;
}

static int wifihal_rssi_to_above_noise_floor(int rssi)
{
    // XXX: This needs to be dynamically read from lower level ...
    rssi -= -95;

    // in case the original rssi is even lower than -95 we cap it:
    if (rssi < 0) rssi = 0;

    return rssi;
}


/******************************************************************************
 *  RADIO CONTROL
 *****************************************************************************/

bool
wifihal_stats_enable(radio_entry_t *radio_cfg, bool enable)
{
    wifihal_radio_t     *radio;
    int                 ret;

    if (!(radio = wifihal_radio_by_ifname(radio_cfg->phy_name))) {
        LOGE("%s: Could not enable stats, radio not found",  radio_cfg->phy_name);
        return false;
    }

    WIFIHAL_TM_START();
    ret = wifi_setRadioStatsEnable(radio->index, enable);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setRadioStatsEnable(%d, %s)",
                                   radio->index, enable ? "true" : "false");

    if (ret != RETURN_OK) {
        LOGE("%s: Failed to enable stats via HAL!!", radio->ifname);
        return false;
    }

    return true;
}

static int auto_rssi_to_above_noise_floor(int rssi)
{
    // if rssi is absolute (negative value) convert to "above noise floor"
    // otherwise return as is
    if (rssi < 0) {
        return wifihal_rssi_to_above_noise_floor(rssi);
    }
    return rssi;
}


/******************************************************************************
 *  CLIENT
 *****************************************************************************/

void wifihal_client_record_free(wifihal_client_record_t *client_entry)
{
    if (client_entry != NULL) {
        free(client_entry);
    }
}

wifihal_client_record_t* wifihal_client_record_alloc()
{
    return calloc(1, sizeof(wifihal_client_record_t));
}

bool wifihal_stats_client_fetch(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list,
        int                         radioIndex,
        int                         apIndex,
        char                       *apName,
        wifi_associated_dev2_t     *assoc_dev)
{
    wifihal_client_record_t *client_entry = NULL;
    wifi_associated_dev_rate_info_rx_stats_t *stats_rx = NULL;
    wifi_associated_dev_rate_info_tx_stats_t *stats_tx = NULL;
    mac_address_str_t mac_str;
    uint64_t handle = 0;
    int ret;
#ifdef BCM_WIFI
    int statIndex = apIndex;
#else
    int statIndex = radioIndex;
#endif

    client_entry = wifihal_client_record_alloc();

    // INFO
    client_entry->info.type = radio_cfg->type;
    memcpy(&client_entry->info.mac, assoc_dev->cli_MACAddress, sizeof(client_entry->info.mac));
    STRLCPY(client_entry->info.ifname, apName);
    STRLCPY(client_entry->info.essid, *essid);
    dpp_mac_to_str(assoc_dev->cli_MACAddress, mac_str);

    // STATS
    memcpy(&client_entry->dev2, assoc_dev, sizeof(client_entry->dev2));

    WIFIHAL_TM_START();
    ret = wifi_getApAssociatedDeviceStats(
            apIndex,
            &assoc_dev->cli_MACAddress,
            &client_entry->stats,
            &handle);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApAssociatedDeviceStats(%d, \"%s\")",
                                            apIndex, mac_str);
    if (ret != RETURN_OK) goto err;

    WIFIHAL_TM_START();
    ret = wifi_getApAssociatedDeviceRxStatsResult(
            statIndex,
            &assoc_dev->cli_MACAddress,
            &stats_rx,
            &client_entry->num_rx,
            &handle);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApAssociatedDeviceRxStatsResult(%d, \"%s\")",
                                            statIndex, mac_str);
    if (ret != RETURN_OK) goto err;
    memcpy(&client_entry->stats_rx, stats_rx, sizeof(*stats_rx) * client_entry->num_rx);
    free(stats_rx);

    WIFIHAL_TM_START();
    ret = wifi_getApAssociatedDeviceTxStatsResult(
            statIndex,
            &assoc_dev->cli_MACAddress,
            &stats_tx,
            &client_entry->num_tx,
            &handle);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApAssociatedDeviceTxStatsResult(%d, \"%s\")",
                                            statIndex, mac_str);
    if (ret != RETURN_OK) goto err;
    memcpy(&client_entry->stats_tx, stats_tx, sizeof(*stats_tx) * client_entry->num_tx);
    free(stats_tx);

    WIFIHAL_TM_START();
    ret = wifi_getApAssociatedDeviceTidStatsResult(
            statIndex,
            &assoc_dev->cli_MACAddress,
            &client_entry->tid_stats,
            &handle);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApAssociatedDeviceTidStatsResult(%d, \"%s\")",
                                            statIndex, mac_str);
    if (ret != RETURN_OK) goto err;

    client_entry->stats_cookie = handle;

out:
    ds_dlist_insert_tail(client_list, client_entry);
    return true;

err:
    LOG(WARNING, "%s: fetch error", __FUNCTION__);
    wifihal_client_record_free(client_entry);
    return false;
}


bool wifihal_stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list)
{
    wifihal_radio_t *radio;
    wifihal_ssid_t *ssid;
    radio_essid_t ssid_name;
    wifi_associated_dev2_t *client_array;
    UINT client_num;
    int ret;
    int i;

    if (!(radio = wifihal_radio_by_ifname(radio_cfg->phy_name))) {
        LOGW("%s: radio not found", radio_cfg->phy_name);
        return false;
    }

    ds_dlist_foreach(&radio->ssids, ssid)
    {
#ifdef BCM_WIFI
        WIFIHAL_TM_START();
        ret = wifi_getSSIDName(ssid->index, ssid_name);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDName(%d) ret \"%s\"",
                               ssid->index, (ret == RETURN_OK) ? ssid_name : "");
#else /* not BCM_WIFI */
        WIFIHAL_TM_START();
        ret = wifi_getSSIDNameStatus(ssid->index, ssid_name);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDNameStatus(%d) ret \"%s\"",
                                     ssid->index, (ret == RETURN_OK) ? ssid_name : "");
#endif /* not BCM_WIFI */
        if (ret != RETURN_OK) continue;
        if (essid && strcmp(*essid, ssid_name)) continue;

        WIFIHAL_TM_START();
        ret = wifi_getApAssociatedDeviceDiagnosticResult2(ssid->index, &client_array, &client_num);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApAssociatedDeviceDiagnosticResult2(%d) ret %u",
                                                          ssid->index, client_num);
        if (ret != RETURN_OK) {
            LOGW("%s %s %d %s: fetch client list",
                 radio_cfg->phy_name, ssid->ifname, ssid->index, ssid_name);
            continue;
        }

        LOGT("%s %s %d %s: fetch client list: %d clients",
             radio_cfg->phy_name, ssid->ifname, ssid->index, ssid_name, client_num);

        for (i=0; i<(int)client_num; i++)
        {
            wifihal_stats_client_fetch(
                    radio_cfg, &ssid_name, client_list,
                    radio->index, ssid->index, ssid->ifname, &client_array[i]);
        }

        free(client_array);
        client_array = NULL;
    }

    return true;
}

bool wifihal_stats_clients_convert(
        radio_entry_t              *radio_cfg,
        wifihal_client_record_t     *data_new,
        wifihal_client_record_t     *data_old,
        dpp_client_record_t        *client_result)
{
    mac_address_str_t mac_str;
    radio_type_t radio_type = radio_cfg->type;

    dpp_mac_to_str(data_new->info.mac, mac_str);

    /*LOG(TRACE,"%s %s n:%p %p %p o:%p %p %p r:%p", __FUNCTION__, mac_str,
            data_new, data_new->stats_tx, data_new->stats_rx,
            data_old, data_old->stats_tx, data_old->stats_rx,
            client_result);*/

#define CALC_DELTA_32(A,B) ((uint32_t)(A) - (uint32_t)(B))

#define GET_DELTA(Y) CALC_DELTA_32(data_new->Y, data_old->Y)

#define ADD_DELTA(X,Y) ADD_DELTA_TO(client_result, X, Y)

#define ADD_DELTA_TO(A,X,Y) \
    do { \
        if (data_old->Y > data_new->Y) { \
            LOGI("Inconsistent data from driver for %s: %lld > %lld. Skipping.",\
                                                    #Y, data_old->Y, data_new->Y); \
            data_old->Y = 0; \
            data_new->Y = 0; \
        } \
        A->X = GET_DELTA(Y); \
        LOG(TRACE, "Client %s stats %s=%llu (delta  %llu - %llu = %llu)", \
                mac_str, #X, (uint64_t)A->X, \
                (uint64_t)data_new->Y, (uint64_t)data_old->Y, (uint64_t)GET_DELTA(Y)); \
    } while (0)


#define GET_AVG_FLOAT(Y) ((((double)data_new->Y) + ((double)data_old->Y)) / 2.0)

#define ASSIGN_AVG_FLOAT(X,Y) \
    do { \
        if (data_new->Y) { \
            if (data_old->Y) { \
                client_result->X = GET_AVG_FLOAT(Y); \
                LOG(TRACE, "Client %s stats %s=%0.2f (avg n:%0.2f o:%0.2f)", \
                        mac_str, #X, (double)client_result->X, \
                        (double)data_new->Y, (double)data_old->Y); \
            } else { \
                client_result->X = (double)data_new->Y; \
                LOG(TRACE, "Client %s stats %s=%0.2f (new)", \
                        mac_str, #X, (double)client_result->X); \
            } \
        } \
    } while (0)


    // STATS

    if (data_new->stats_cookie != data_old->stats_cookie)
    {
        // This a new connection since last stats reading, so the
        // deltas cannot be calculated against the old one.
        // Calculate deltas against "0", so effectively just return
        // whatever the driver reported with the last stat reading.

        LOGD("New connection - clear old stat records");
        memset(&data_old->stats, 0, sizeof(data_old->stats));
        memset(&data_old->dev2, 0, sizeof(data_old->dev2));
        memset(&data_old->stats_rx, 0, sizeof(data_old->stats_rx));
        memset(&data_old->stats_tx, 0, sizeof(data_old->stats_tx));
        memset(&data_old->tid_stats, 0, sizeof(data_old->tid_stats));
        data_old->num_rx = 0;
        data_old->num_tx = 0;
    }

    ADD_DELTA(stats.bytes_tx,   stats.cli_tx_bytes);
    ADD_DELTA(stats.bytes_rx,   stats.cli_rx_bytes);
    ADD_DELTA(stats.frames_tx,  stats.cli_tx_frames);
    ADD_DELTA(stats.frames_rx,  stats.cli_rx_frames);
    ADD_DELTA(stats.retries_tx, stats.cli_tx_retries);
    ADD_DELTA(stats.retries_rx, stats.cli_rx_retries);
    ADD_DELTA(stats.errors_tx,  stats.cli_tx_errors);
    ADD_DELTA(stats.errors_rx,  stats.cli_rx_errors);

    client_result->stats.rssi = auto_rssi_to_above_noise_floor(data_new->dev2.cli_RSSI);
    LOG(TRACE, "Client %s stats %s=%d", mac_str, "stats.rssi", client_result->stats.rssi);

    ASSIGN_AVG_FLOAT(stats.rate_tx, dev2.cli_LastDataUplinkRate / 1000.0);
    ASSIGN_AVG_FLOAT(stats.rate_rx, dev2.cli_LastDataDownlinkRate / 1000.0);

    // RX STATS

    int n; // index in new record
    int o; // index in old record
    bool found;

    dpp_client_stats_rx_t *client_stats_rx = NULL;

#define DPP_RX(X) client_stats_rx->X
#define NEW_RX(X) (n >= 0 ? data_new->stats_rx[n].X : 0)
#define OLD_RX(X) (o >= 0 ? data_old->stats_rx[o].X : 0)

#if BCM_WIFI
#define GET_DELTA_RX(Y) NEW_RX(Y)
#else
#define GET_DELTA_RX(Y) CALC_DELTA_32(NEW_RX(Y), OLD_RX(Y))
#endif

#define ADD_DELTA_RX(X,Y) \
    do { \
        if (OLD_RX(Y) > NEW_RX(Y)) { \
            LOGI("Inconsistent data from driver for %s: %lld > %lld. Skipping.",\
                                                    #Y, OLD_RX(Y), NEW_RX(Y)); \
            if (o >= 0) data_old->stats_rx[o].Y = 0; \
            if (n >= 0) data_new->stats_rx[n].Y = 0; \
        } \
        DPP_RX(X) += GET_DELTA_RX(Y); \
        LOG(TRACE, "Client %s stats_rx [%d %d %d] %s=%llu (delta %llu - %llu = %llu)", \
                mac_str, DPP_RX(bw), DPP_RX(nss), DPP_RX(mcs), #X, (uint64_t)DPP_RX(X), \
                (uint64_t)NEW_RX(Y), (uint64_t)OLD_RX(Y), (uint64_t)GET_DELTA_RX(Y)); \
    } while (0)

#ifdef BCM_WIFI
    // Convert "bw" to index, if needed
    for (n = 0; n < (int)data_new->num_rx; n++)
    {
        switch (NEW_RX(bw))
        {
        case 20:
            data_new->stats_rx[n].bw = 0;
            break;

        case 40:
            data_new->stats_rx[n].bw = 1;
            break;

        case 80:
            data_new->stats_rx[n].bw = 2;
            break;

        default:
            break;
        }
    }
#endif /* BCM_WIFI */

    //wifihal_dump_client_record(data_new, "new");
    //wifihal_dump_client_record(data_old, "old");

    for (n = 0; n < (int)data_new->num_rx; n++)
    {
        // find matching index in old record
        found = false;
        for (o = 0; o < (int)data_old->num_rx; o++)
        {
            if (   (NEW_RX(mcs) == OLD_RX(mcs))
                && (NEW_RX(nss) == OLD_RX(nss))
                && (NEW_RX(bw)  == OLD_RX(bw)) )
            {
                found = true;
                break;
            }
        }
        if (!found) o = -1;

        // Skip unchanged entries
        if (   !GET_DELTA_RX(bytes)
            && !GET_DELTA_RX(msdus)
            && !GET_DELTA_RX(mpdus)
            && !GET_DELTA_RX(ppdus)
            && !GET_DELTA_RX(retries) )
        {
            continue;
        }

        client_stats_rx = dpp_client_stats_rx_record_alloc();
        if (client_stats_rx == NULL) {
            LOG(ERR,
                "Updating %s interface client stats rx"
                "(Failed to allocate memory)",
                radio_get_name_from_type(radio_type));
            return false;
        }

        DPP_RX(mcs)     = NEW_RX(mcs);
        DPP_RX(nss)     = NEW_RX(nss);
        DPP_RX(bw)      = NEW_RX(bw);
        // auto detect rssi format based on cli_RSSI sign
        if (data_new->dev2.cli_RSSI < 0) {
            // rssi reported as negated absolute value - convert
            DPP_RX(rssi) = wifihal_rssi_to_above_noise_floor(-(int)NEW_RX(rssi_combined));
        } else {
            // rssi reported as value above noise floor - use as is
            DPP_RX(rssi) = NEW_RX(rssi_combined);
        }
        DPP_RX(errors)  = 0; // not collecting currently

        ADD_DELTA_RX(bytes,     bytes);
        ADD_DELTA_RX(msdu,      msdus);
        ADD_DELTA_RX(mpdu,      mpdus);
        ADD_DELTA_RX(ppdu,      ppdus);
        ADD_DELTA_RX(retries,   retries);

        ds_dlist_insert_tail(&client_result->stats_rx, client_stats_rx);
    }

    // TX STATS
    dpp_client_stats_tx_t *client_stats_tx = NULL;

#define DPP_TX(X) client_stats_tx->X
#define NEW_TX(X) (n >= 0 ? data_new->stats_tx[n].X : 0)
#define OLD_TX(X) (o >= 0 ? data_old->stats_tx[o].X : 0)

#ifdef BCM_WIFI
#define GET_DELTA_TX(Y) NEW_TX(Y)
#else
#define GET_DELTA_TX(Y) CALC_DELTA_32(NEW_TX(Y), OLD_TX(Y))
#endif

#define ADD_DELTA_TX(X,Y) \
    do { \
        if (OLD_TX(Y) > NEW_TX(Y)) { \
            LOGI("Inconsistent data from driver for %s: %lld > %lld. Skipping.",\
                                                    #Y, OLD_TX(Y), NEW_TX(Y)); \
            if (o >= 0) data_old->stats_tx[o].Y = 0; \
            if (n >= 0) data_new->stats_tx[n].Y = 0; \
        } \
        DPP_TX(X) = GET_DELTA_TX(Y); \
        LOG(TRACE, "Client %s stats_tx [%d %d %d] %s=%llu (delta %llu - %llu = %llu)", \
                mac_str, DPP_TX(bw), DPP_TX(nss), DPP_TX(mcs), #X, (uint64_t)DPP_TX(X), \
                (uint64_t)NEW_TX(Y), (uint64_t)OLD_TX(Y), (uint64_t)GET_DELTA_TX(Y)); \
    } while (0)

#ifdef BCM_WIFI
    // Convert "bw" to index, if needed
    for (n = 0; n < (int)data_new->num_tx; n++)
    {
        switch (NEW_TX(bw))
        {
        case 20:
            data_new->stats_tx[n].bw = 0;
            break;

        case 40:
            data_new->stats_tx[n].bw = 1;
            break;

        case 80:
            data_new->stats_tx[n].bw = 2;
            break;

        default:
            break;
        }
    }
#endif /* BCM_WIFI */

    for (n = 0; n < (int)data_new->num_tx; n++)
    {
        // find matching index in old record
        found = false;
        for (o = 0; o < (int)data_old->num_tx; o++)
        {
            if (   (NEW_TX(mcs) == OLD_TX(mcs))
                && (NEW_TX(nss) == OLD_TX(nss))
                && (NEW_TX(bw)  == OLD_TX(bw)) )
            {
                found = true;
                break;
            }
        }
        if (!found) o = -1;

        // skip unchanged entries
        if (   !GET_DELTA_TX(attempts)
            && !GET_DELTA_TX(mpdus) )
        {
            continue;
        }

        client_stats_tx = dpp_client_stats_tx_record_alloc();
        if (client_stats_tx == NULL) {
            LOG(ERR,
                    "Updating %s interface client stats tx"
                    "(Failed to allocate memory)",
                    radio_get_name_from_type(radio_type));
            return false;
        }

        DPP_TX(mcs)     = NEW_TX(mcs);
        DPP_TX(nss)     = NEW_TX(nss);
        DPP_TX(bw)      = NEW_TX(bw);

        // not collecting currently
        DPP_TX(bytes)   = 0;
        DPP_TX(msdu)    = 0;
        DPP_TX(errors)  = 0;

        ADD_DELTA_TX(mpdu,      mpdus);
        ADD_DELTA_TX(ppdu,      ppdus);
        ADD_DELTA_TX(retries,   retries);

        ds_dlist_insert_tail(&client_result->stats_tx, client_stats_tx);
    }

    // TID STATS

    dpp_client_tid_record_list_t   *record = NULL;

    // Add new measurement for every convert
    record = dpp_client_tid_record_alloc();
    if (record == NULL) {
        LOG(ERR, "Updating client %s tid stats (Failed to allocate memory)", mac_str);
        return false;
    }
    record->timestamp_ms = get_timestamp();
    ds_dlist_insert_tail(&client_result->tid_record_list, record);

    int i;
    for (i = 0; i < ARRAY_LEN(data_new->tid_stats.tid_array); i++)
    {
        if (   !GET_DELTA(tid_stats.tid_array[i].sum_time_ms)
            && !GET_DELTA(tid_stats.tid_array[i].num_msdus) )
        {
            continue;
        }
        record->entry[i].ac             = data_new->tid_stats.tid_array[i].ac;
        record->entry[i].tid            = data_new->tid_stats.tid_array[i].tid;
        record->entry[i].ewma_time_ms   = data_new->tid_stats.tid_array[i].ewma_time_ms;
        ADD_DELTA_TO(record, entry[i].sum_time_ms,  tid_stats.tid_array[i].sum_time_ms);
        ADD_DELTA_TO(record, entry[i].num_msdus,    tid_stats.tid_array[i].num_msdus);
    }

    return true;
}


/******************************************************************************
 *  SURVEY
 *****************************************************************************/

bool wifihal_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        ds_dlist_t                 *survey_list)
{
    int i;
    int ret;
    int radioIndex = 0;
    wifihal_survey_record_t *survey_record;

    // phy_name
    if (!radio_entry_to_hal_radio_index(radio_cfg, &radioIndex)) {
        return false;
    }

    // Mark requested channels
    wifihal_survey_data_t survey_data;
    memset(&survey_data, 0, sizeof(survey_data));
    for (i = 0; i < (int)chan_num; i++)
    {
        survey_data.chan[i].ch_number = chan_list[i];
        survey_data.chan[i].ch_in_pool = true;
    }

    WIFIHAL_TM_START();
    ret = wifi_getRadioChannelStats(radioIndex, survey_data.chan, chan_num);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioChannelStats(%d)",
                                    radioIndex);
    if (ret != RETURN_OK) return false;

    survey_data.num_chan = chan_num;
    survey_data.timestamp_ms = get_timestamp();

    // Assume that all were collected and stored into the array
    for (i = 0; i < (int)survey_data.num_chan; i++)
    {
        survey_record = wifihal_survey_record_alloc();
        if (survey_record == NULL) {
            LOGE("Processing %s %s survey report (Failed to allocate memory)",
                 radio_get_name_from_type(radio_cfg->type),
                 radio_get_scan_name_from_type(scan_type));
            return false;
        }

        if (scan_type == RADIO_SCAN_TYPE_ONCHAN)
        {
            survey_record->info.chan = chan_list[i];
            survey_record->info.timestamp_ms = get_timestamp();

            survey_record->stats.survey_bss.chan_active   = survey_data.chan[i].ch_utilization_total;
            survey_record->stats.survey_bss.chan_busy     = survey_data.chan[i].ch_utilization_busy;
            survey_record->stats.survey_bss.chan_tx       = survey_data.chan[i].ch_utilization_busy_tx;
            survey_record->stats.survey_bss.chan_self     = survey_data.chan[i].ch_utilization_busy_self;
            survey_record->stats.survey_bss.chan_rx       = survey_data.chan[i].ch_utilization_busy_rx;
            survey_record->stats.survey_bss.chan_busy_ext = survey_data.chan[i].ch_utilization_busy_ext;

            LOGT("Fetched %s %s %u survey "
                 "{active=%llu busy=%llu tx=%llu self=%llu rx=%llu ext=%llu}",
                 radio_get_name_from_type(radio_cfg->type),
                 radio_get_scan_name_from_type(scan_type),
                 survey_record->info.chan,
                 survey_record->stats.survey_bss.chan_active,
                 survey_record->stats.survey_bss.chan_busy,
                 survey_record->stats.survey_bss.chan_tx,
                 survey_record->stats.survey_bss.chan_self,
                 survey_record->stats.survey_bss.chan_rx,
                 survey_record->stats.survey_bss.chan_busy_ext);
        }
        else
        {
            survey_record->info.chan = chan_list[i];
            survey_record->info.timestamp_ms = get_timestamp();

            survey_record->stats.survey_obss.chan_active   = (uint32_t)survey_data.chan[i].ch_utilization_total;
            survey_record->stats.survey_obss.chan_busy     = (uint32_t)survey_data.chan[i].ch_utilization_busy;
            survey_record->stats.survey_obss.chan_tx       = (uint32_t)survey_data.chan[i].ch_utilization_busy_tx;
            survey_record->stats.survey_obss.chan_self     = (uint32_t)survey_data.chan[i].ch_utilization_busy_self;
            survey_record->stats.survey_obss.chan_rx       = (uint32_t)survey_data.chan[i].ch_utilization_busy_rx;
            survey_record->stats.survey_obss.chan_busy_ext = (uint32_t)survey_data.chan[i].ch_utilization_busy_ext;

            LOGT("Fetched %s %s %u survey "
                 "{active=%u busy=%u tx=%u self=%u rx=%u ext=%u}",
                 radio_get_name_from_type(radio_cfg->type),
                 radio_get_scan_name_from_type(scan_type),
                 survey_record->info.chan,
                 survey_record->stats.survey_obss.chan_active,
                 survey_record->stats.survey_obss.chan_busy,
                 survey_record->stats.survey_obss.chan_tx,
                 survey_record->stats.survey_obss.chan_self,
                 survey_record->stats.survey_obss.chan_rx,
                 survey_record->stats.survey_obss.chan_busy_ext);
        }

        ds_dlist_insert_tail(survey_list, survey_record);
    }

    return true;
}

bool wifihal_stats_survey_convert(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        wifihal_survey_record_t    *data_new,
        wifihal_survey_record_t    *data_old,
        dpp_survey_record_t        *survey_record)
{
    radio_type_t                    radio_type;

    if ((!data_new) || (!data_old) || (!survey_record)) {
        return false;
    }
    radio_type = radio_cfg->type;

#define PERCENT(v1, v2) (v2 > 0 ? (v1*100/v2) : 0)

#define DELTA_TYPE(TYPE, NEW, OLD) (STATS_CUMULATIVE_##TYPE ? (NEW - OLD) : NEW)
#define XDELTA_TYPE(TYPE, F) DELTA_TYPE(TYPE, data_new->stats.F, data_old->stats.F)

#define XDELTA_ONCHAN(F)  XDELTA_TYPE(SURVEY_ONCHAN, F)
#define XDELTA_OFFCHAN(F) XDELTA_TYPE(SURVEY_OFFCHAN, F)


    if (scan_type == RADIO_SCAN_TYPE_ONCHAN)
    {
        wifihal_survey_bss_t     data;

        data.chan_active    = XDELTA_ONCHAN(survey_bss.chan_active);
        data.chan_tx        = XDELTA_ONCHAN(survey_bss.chan_tx);
        data.chan_rx        = XDELTA_ONCHAN(survey_bss.chan_rx);
        data.chan_busy      = XDELTA_ONCHAN(survey_bss.chan_busy);
        data.chan_busy_ext  = XDELTA_ONCHAN(survey_bss.chan_busy_ext);
        data.chan_self      = XDELTA_ONCHAN(survey_bss.chan_self);

        LOG(TRACE,
            "Processed %s %s survey delta "
            "{active=%llu busy=%llu tx=%llu self=%llu rx=%llu ext=%llu}",
            radio_get_name_from_type(radio_type),
            radio_get_scan_name_from_type(scan_type),
            data.chan_active,
            data.chan_busy,
            data.chan_tx,
            data.chan_self,
            data.chan_rx,
            data.chan_busy_ext);

        // Repeat the measurement
        if (!data.chan_active) return false;

        survey_record->chan_busy     = PERCENT(data.chan_busy, data.chan_active);
        survey_record->chan_tx       = PERCENT(data.chan_tx, data.chan_active);
        survey_record->chan_rx       = PERCENT(data.chan_rx, data.chan_active);
        survey_record->chan_self     = PERCENT(data.chan_self, data.chan_active);
        survey_record->chan_busy_ext = PERCENT(data.chan_busy_ext, data.chan_active);
        survey_record->duration_ms   = data.chan_active / 1000;
    }
    else /* OFF and FULL */
    {
        wifihal_survey_obss_t     data;

        data.chan_active    = XDELTA_OFFCHAN(survey_obss.chan_active);
        data.chan_tx        = XDELTA_OFFCHAN(survey_obss.chan_tx);
        data.chan_rx        = XDELTA_OFFCHAN(survey_obss.chan_rx);
        data.chan_busy      = XDELTA_OFFCHAN(survey_obss.chan_busy);
        data.chan_busy_ext  = XDELTA_OFFCHAN(survey_obss.chan_busy_ext);
        data.chan_self      = XDELTA_OFFCHAN(survey_obss.chan_self);

        LOG(TRACE,
            "Processed %s %s survey delta "
            "{active=%u busy=%u tx=%u self=%u rx=%u ext=%u}",
            radio_get_name_from_type(radio_type),
            radio_get_scan_name_from_type(scan_type),
            data.chan_active,
            data.chan_busy,
            data.chan_tx,
            data.chan_self,
            data.chan_rx,
            data.chan_busy_ext);

        // Repeat the measurement
        if (!data.chan_active) return false;

        survey_record->chan_busy     = PERCENT(data.chan_busy, data.chan_active);
        survey_record->chan_tx       = PERCENT(data.chan_tx, data.chan_active);
        survey_record->chan_rx       = PERCENT(data.chan_rx, data.chan_active);
        survey_record->duration_ms   = data.chan_active / 1000;
    }

    return true;
}

bool wifihal_stats_scan_initiate(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time)
{
    int apIndex = 0;
    wifihal_ssid_t *ap;
    wifi_neighborScanMode_t scan_mode;
    uint32_t *c;
    uint32_t i;
    char buf[1024];
    char tmp[32];
    int ret;

    if (!(ap = wifihal_ssid_by_ifname(radio_cfg->if_name))) {
        LOGW("%s: ap not found", radio_cfg->if_name);
        return false;
    }
    apIndex = ap->index;
#ifdef QTN_WIFI
    apIndex &= 0x1;
#endif

    if (scan_type == RADIO_SCAN_TYPE_ONCHAN) {
        scan_mode = WIFI_RADIO_SCAN_MODE_ONCHAN;
    } else {
        scan_mode = WIFI_RADIO_SCAN_MODE_OFFCHAN;
    }

    buf[0] = '\0';
    c = chan_list;
    for (i = 0; i < chan_num; i++)
    {
        sprintf(tmp, "%u", *c++);
        if (buf[0] != '\0') {
            strcat(buf, " ");
        }
        strcat(buf, tmp);
    }

    WIFIHAL_TM_START();
    ret = wifi_startNeighborScan(apIndex, scan_mode, dwell_time, chan_num, chan_list);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_startNeighborScan(%d, %s, dwell: %zu, count: %zu, chans: %s)",
                                 apIndex, (scan_mode == WIFI_RADIO_SCAN_MODE_ONCHAN) ? "on-chan" : "off-chan",
                                                     dwell_time, chan_num, buf);

    if (ret != RETURN_OK) return false;

    return true;
}


/******************************************************************************
 *  SCAN
 *****************************************************************************/

typedef struct
{
    radio_entry_t                  *radio_cfg;
    radio_scan_type_t               scan_type;
    wifihal_scan_cb_t              *scan_cb;
    void                           *scan_ctx;
} wifihal_scan_request_t;

#define WIFIHAL_SCAN_RESULT_POLL_TIME       0.2
// Need to wait 20s for FULL chan results
#define WIFIHAL_SCAN_RESULT_POLL_TIMEOUT    100  // 100 * 0.2 = 20 sec

static wifi_neighbor_ap2_t* g_scan_results = NULL;
static uint32_t             g_scan_results_size;
static ev_timer             g_scan_result_timer;
static int32_t              g_scan_result_timeout;

static void wifihal_scan_result_timer_set(
        ev_timer                   *timer,
        bool                        enable)
{
    if (enable)
    {
        ev_timer_again(EV_DEFAULT, timer);
    }
    else
    {
        ev_timer_stop(EV_DEFAULT, timer);
    }
}

static void wifihal_stats_scan_results_fetch(EV_P_ ev_timer *w, int revents)
{
    int                          scan_status = false;
    wifihal_scan_request_t      *request_ctx = (wifihal_scan_request_t *) w->data;
    radio_entry_t               *radio_cfg = request_ctx->radio_cfg;
    radio_type_t                 radio_type = radio_cfg->type;
    radio_scan_type_t            scan_type = request_ctx->scan_type;
    wifihal_ssid_t *ap;
    int apIndex = 0;
    bool ret;

    // The driver scans and adds results to the buffer specified.
    // Since we do no know when scanning is finished, we need to poll for info.
    // We poll in steps of 250ms. Max waiting time is 5s.

    if (!(ap = wifihal_ssid_by_ifname(radio_cfg->if_name)))
    {
        LOGW("%s: ap not found", radio_cfg->if_name);
        goto exit;
    }
    apIndex = ap->index;

    free(g_scan_results);
    g_scan_results = NULL;

    WIFIHAL_TM_START();
    ret = wifi_getNeighboringWiFiStatus(apIndex, &g_scan_results, &g_scan_results_size);
    WIFIHAL_TM_STOP_NORET(5, "wifi_getNeighboringWiFiStatus(%d) ret sz %zu = %d [errno %d]",
                                        apIndex, g_scan_results_size, ret, errno);
    if (ret != RETURN_OK)
    {
        // Scanning is still in progress ... come back later
        if (errno == EAGAIN)
        {
            LOG(TRACE,
                    "Parsing %s %s scan (EAGAIN - retry later...)",
                    radio_get_name_from_type(radio_type),
                    radio_get_scan_name_from_type(scan_type));

            if (--g_scan_result_timeout > 0)
            {
                goto restart_timer;
            }

            LOG(ERR,
                    "Parsing %s %s scan (timeout occurred)",
                    radio_get_name_from_type(radio_type),
                    radio_get_scan_name_from_type(scan_type));
            goto exit;
        }

        // Scanning is finished but needs more space for results
        if (errno == E2BIG)
        {
            LOG(ERR,
                    "Parsing %s %s scan (E2BIG issue)",
                    radio_get_name_from_type(radio_type),
                    radio_get_scan_name_from_type(scan_type));
            goto exit;
        }

        LOG(ERR,
                "Parsing %s %s scan for %s ('%s')",
                radio_get_name_from_type(radio_type),
                radio_get_scan_name_from_type(scan_type),
                radio_cfg->if_name,
                strerror(errno));
        goto exit;
    }

    // Mark results scan_status
    scan_status = true;

exit:
    wifihal_scan_result_timer_set(w, false);
    g_scan_result_timeout = WIFIHAL_SCAN_RESULT_POLL_TIMEOUT;

clean:
    // Notify upper layer about scan status (blocking)
    if (request_ctx->scan_cb)
    {
        request_ctx->scan_cb(request_ctx->scan_ctx, scan_status);
    }

restart_timer:
    return;
}

bool wifihal_stats_scan_start(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        wifihal_scan_cb_t          *scan_cb,
        void                       *scan_ctx)
{
    if (!wifihal_stats_scan_initiate(
                radio_cfg,
                chan_list,
                chan_num,
                scan_type,
                dwell_time))
    {
        return false;
    }

    static wifihal_scan_request_t scan_request;  /* TODO: unify sm_scan_request */

    memset (&scan_request, 0, sizeof(scan_request));
    scan_request.radio_cfg  = radio_cfg;
    scan_request.scan_type  = scan_type;
    scan_request.scan_cb    = scan_cb;
    scan_request.scan_ctx   = scan_ctx;

    // Start result polling timer
    ev_init (&g_scan_result_timer, wifihal_stats_scan_results_fetch);
    g_scan_result_timer.repeat =  WIFIHAL_SCAN_RESULT_POLL_TIME;
    g_scan_result_timer.data = &scan_request;
    wifihal_scan_result_timer_set(&g_scan_result_timer, true);
    g_scan_result_timeout = WIFIHAL_SCAN_RESULT_POLL_TIMEOUT;

    return true;
}

bool wifihal_stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    wifihal_scan_result_timer_set(&g_scan_result_timer, false);
    return true;
}

void wifihal_stats_scan_hal_to_dpp_record(
        wifi_neighbor_ap2_t *hal,
        radio_type_t radio_type,
        dpp_neighbor_record_t *entry)
{
    entry->type = radio_type;
    memcpy(entry->bssid, hal->ap_BSSID, sizeof(entry->bssid));
    strlcpy(entry->ssid, hal->ap_SSID, sizeof(entry->ssid));
    entry->chan = hal->ap_Channel;
    entry->sig = auto_rssi_to_above_noise_floor(hal->ap_SignalStrength);
    entry->lastseen = time(NULL);
    entry->chanwidth = phymode_to_chanwidth(hal->ap_OperatingChannelBandwidth);
    //uint64_t tsf; // not available
}

bool wifihal_stats_scan_hal_to_dpp_record_array(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        wifi_neighbor_ap2_t        *hal_array,
        uint32_t                    hal_num,
        dpp_neighbor_record_t      *scan_records,
        int                         scan_records_size,
        int                        *scan_result_qty)
{
    radio_type_t radio_type = radio_cfg->type;
    wifi_neighbor_ap2_t *hal = NULL;
    dpp_neighbor_record_t *entry;
    int i;

    *scan_result_qty = 0;
    for (i = 0; i < (int)hal_num; i++)
    {
        hal = &hal_array[i];
        if (*scan_result_qty >= scan_records_size)
        {
            LOG(WARNING, "too many neighbors %d %d %d",
                    i, *scan_result_qty, scan_records_size);
            break;
        }

#ifdef QTN_WIFI
        // XXX WAR: Quantenna is returning negated value of "above noise floor" signal
        if (hal->ap_SignalStrength < 0) {
            hal->ap_SignalStrength *= -1;
        }
#endif /* QTN_WIFI */

        entry = &scan_records[*scan_result_qty];
        (*scan_result_qty)++;
        wifihal_stats_scan_hal_to_dpp_record(hal, radio_type, entry);
    }

    return true;
}

bool wifihal_stats_scan_hal_to_dpp_report_list(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        wifi_neighbor_ap2_t        *hal_array,
        uint32_t                    hal_num,
        dpp_neighbor_report_data_t *scan_results)
{
    radio_type_t radio_type = radio_cfg->type;
    wifi_neighbor_ap2_t *hal = NULL;
    dpp_neighbor_list_t *neighbor_list = &scan_results->list;
    dpp_neighbor_record_list_t *neighbor = NULL;
    dpp_neighbor_record_t *entry;
    int i;

    for (i = 0; i < (int)hal_num; i++)
    {
        hal = &hal_array[i];
        neighbor = dpp_neighbor_record_alloc();
        if (neighbor == NULL) {
            LOG(ERR, "Parsing %d %d neighbor stats (alloc)", radio_type, scan_type);
            return false;
        }
        entry = &neighbor->entry;
        wifihal_stats_scan_hal_to_dpp_record(hal, radio_type, entry);
        ds_dlist_insert_tail(neighbor_list, neighbor);
    }

    return true;
}


bool wifihal_scan_extract_neighbors_from_ssids(
        radio_type_t                radio_type,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_record_t      *scan_results,
        uint32_t                    scan_result_qty,
        dpp_neighbor_list_t        *neighbor_list)
{
#define LAST_5_BYTE_EQ(bssid1, bssid2)  (strncmp(bssid1+3, bssid2+3, 14) == 0)
#define BSSID_CMP(bssid1, bssid2)       strcmp(bssid1+8, bssid2+8)
    dpp_neighbor_record_t          *rec_new;
    uint32_t                        rec_new_count=0;
    dpp_neighbor_record_t          *rec_cmp;
    uint32_t                        rec_cmp_count=0;

    uint32_t                        chan_index;
    uint32_t                        chan_found = false;

    dpp_neighbor_record_list_t     *neighbor = NULL;
    dpp_neighbor_record_t          *neighbor_entry = NULL;
    uint32_t                        neighbor_qty = 0;

    if (    (scan_results == NULL)
         || (neighbor_list == NULL)
       )
    {
        return false;
    }

    // Remove multiple SSID's per neighbor AP
    for (   rec_new_count = 0;
            rec_new_count < scan_result_qty;
            rec_new_count++)
    {
        rec_new = &scan_results[rec_new_count];

        // Skip entries that are not seen
        if (!rec_new->lastseen)
        {
            continue;
        }

        // Skip entries that are not on scanned channel
        chan_found = false;
        for (   chan_index = 0;
                chan_index < chan_num;
                chan_index++)
        {
            if (rec_new->chan == chan_list[chan_index])
            {
                chan_found = true;
                break;
            }
        }

        if (!chan_found)
        {
            continue;
        }

        // Find duplicate entries and mark them not seen
        for (   rec_cmp_count = rec_new_count + 1;
                rec_cmp_count < scan_result_qty;
                rec_cmp_count++)
        {
            rec_cmp = &scan_results[rec_cmp_count];

            if (rec_new->chan != rec_cmp->chan)
            {
                continue;
            }

            // Skip duplicate entries
            if (strcmp(rec_new->bssid, rec_cmp->bssid) == 0)
            {
                rec_cmp->lastseen = 0;
                continue;
            }
        }

        neighbor = dpp_neighbor_record_alloc();
        if (neighbor == NULL)
        {
            LOG(ERR,
                "Parsing %s %s interface neighbor stats "
                "(Failed to allocate memory)",
                radio_get_name_from_type(radio_type),
                radio_get_scan_name_from_type(scan_type));
            return false;
        }
        neighbor_entry = &neighbor->entry;

        memcpy (neighbor_entry,
                rec_new,
                sizeof(dpp_neighbor_record_t));

        ds_dlist_insert_tail(neighbor_list, neighbor);
        neighbor_qty++;
    }

    LOG(TRACE,
        "Parsing %s %s scan (removed %d entries of %d)",
        radio_get_name_from_type(radio_type),
        radio_get_scan_name_from_type(scan_type),
        (scan_result_qty - neighbor_qty),
        scan_result_qty);

    return true;
}

bool wifihal_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results)
{
    dpp_neighbor_record_t scan_records[WIFIHAL_SCAN_MAX_RECORDS];
    int scan_result_qty = 0;
    radio_type_t radio_type;
    bool ret;

    if (scan_results == NULL) {
        return false;
    }

    radio_type = radio_cfg->type;

    ret = wifihal_stats_scan_hal_to_dpp_record_array(
            radio_cfg,
            chan_list,
            chan_num,
            scan_type,
            g_scan_results,
            g_scan_results_size,
            scan_records,
            ARRAY_SIZE(scan_records),
            &scan_result_qty);
    if (!ret)
    {
        LOG(ERR,
                "Fetch %s %s scan",
                radio_get_name_from_type(radio_type),
                radio_get_scan_name_from_type(scan_type));
        return false;
    }

    // Remove multiple SSID's per neighbor AP and strip results for
    // on-channel scanning (driver seems to scan the selected channel
    // only, but returns entries (cache) for all)
    bool success = wifihal_scan_extract_neighbors_from_ssids (
            radio_type,
            chan_list,
            chan_num,
            scan_type,
            scan_records,
            scan_result_qty,
            &scan_results->list);
    if (!success)
    {
        LOG(ERR,
            "Parsing %s %s scan (remove neighbor SSID)",
            radio_get_name_from_type(radio_type),
            radio_get_scan_name_from_type(scan_type));
        return false;
    }

    LOG(TRACE,
            "Parsed %s %s scan results for channel %d",
            radio_get_name_from_type(radio_type),
            radio_get_scan_name_from_type(scan_type),
            chan_list[0]);

    return true;
}


/******************************************************************************
 *  CAPACITY
 *****************************************************************************/

bool wifihal_stats_capacity_get(
        radio_entry_t              *radio_cfg,
        wifihal_capacity_data_t     *capacity_result)
{
    wifihal_radio_t *radio;
    wifihal_ssid_t *ssid;
    int radioIndex = 0;
    int ret;

    if (!(radio = wifihal_radio_by_ifname(radio_cfg->phy_name))) {
        LOGW("%s: radio not found", radio_cfg->phy_name);
        return false;
    }
    radioIndex = radio->index;

    memset(capacity_result, 0, sizeof(*capacity_result));

    ds_dlist_foreach(&radio->ssids, ssid)
    {
        wifi_ssidTrafficStats2_t ssid_stats;
        WIFIHAL_TM_START();
        ret = wifi_getSSIDTrafficStats2(ssid->index, &ssid_stats);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDTrafficStats2(%d)",
                                        ssid->index);
        if (ret != RETURN_OK) return false;
        capacity_result->bytes_tx += ssid_stats.ssid_BytesSent;
    }

    wifi_channelStats_t chan_stats;
    chan_stats.ch_number = radio_cfg->chan;
    // num 0 means onchan (bss)
    WIFIHAL_TM_START();
    ret = wifi_getRadioChannelStats(radioIndex, &chan_stats, 0);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioChannelStats(%d, 0)",
                                    radioIndex);
    if (ret != RETURN_OK) return false;
    capacity_result->chan_active = chan_stats.ch_utilization_total;
    capacity_result->chan_tx = chan_stats.ch_utilization_busy_tx;

    // capacity queue util not used anymore

    return true;
}
