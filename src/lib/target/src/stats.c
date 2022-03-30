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
#include "const.h"

#include "target.h"
#include "target_internal.h"

#define MODULE_ID LOG_MODULE_ID_OSA

#define STATS_SCAN_MAX_RECORDS     300

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
};  // TODO: should go to vendor layer if different

typedef bool stats_scan_cb_t(
        void                       *scan_ctx,
        int                         status);

#define STATS_SURVEY_CHAN_MAX 64
typedef struct
{
    uint64_t            timestamp_ms;
    uint32_t            num_chan;
    wifi_channelStats_t chan[STATS_SURVEY_CHAN_MAX];
} survey_data_t;

bool stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list);

bool stats_clients_convert(
        radio_entry_t              *radio_cfg,
        stats_client_record_t      *data_new,
        stats_client_record_t      *data_old,
        dpp_client_record_t        *client_result);

bool stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        ds_dlist_t                 *survey_list);

bool stats_survey_convert(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        stats_survey_record_t      *data_new,
        stats_survey_record_t      *data_old,
        dpp_survey_record_t        *survey_record);

bool stats_scan_start(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        stats_scan_cb_t            *scan_cb,
        void                       *scan_ctx);

bool stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type);

bool stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results);

bool stats_capacity_get(
        radio_entry_t              *radio_cfg,
        stats_capacity_data_t    *capacity_result);

static radio_chanwidth_t phymode_to_chanwidth(char *phymode)
{
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(g_phymode_bw_table); i++)
    {
        if (strcmp(phymode, g_phymode_bw_table[i].phymode) == 0)
        {
            return g_phymode_bw_table[i].chanwidth;
        }
    }

    // for unknown return 20MHz
    return RADIO_CHAN_WIDTH_20MHZ;
}

static inline stats_survey_record_t* stats_survey_record_alloc()
{
    stats_survey_record_t *record = NULL;

    record = malloc(sizeof(stats_survey_record_t));
    if (record != NULL)
    {
        memset(record, 0, sizeof(stats_survey_record_t));
    }

    return record;
}

static inline void stats_survey_record_free(stats_survey_record_t *record)
{
    if (record != NULL)
    {
        free(record);
    }
}

int stats_mcs_nss_bw_to_dpp_index(int mcs, int nss, int bw)
{
    if (!nss) return mcs;

    return
        STATS_LEGACY_RECORDS
        + mcs
        + (nss-1) * STATS_MSC_QTY
        + bw * STATS_MSC_QTY * STATS_NSS_QTY;
}

static bool radio_entry_to_hal_radio_index(radio_entry_t *radio_cfg, int *radioIndex)
{
    INT ret;
    ULONG r, rnum;
    char radio_ifname[128];

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to get radio count", __func__);
        return false;
    }

    *radioIndex = -1;
    for (r = 0; r < rnum; r++)
    {
        memset(radio_ifname, 0, sizeof(radio_ifname));
        ret = wifi_getRadioIfName(r, radio_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: failed to get radio ifname for idx %ld\n", __func__, r);
            return false;
        }
        if (!strncmp(radio_ifname, radio_cfg->phy_name, sizeof(radio_ifname)))
        {
            *radioIndex = r;
            break;
        }
    }

    if (*radioIndex == -1)
    {
        LOGE("%s: cannot find radio index for %s", __func__, radio_cfg->phy_name);
        return false;
    }

    return true;
}

static int rssi_to_above_noise_floor(int rssi)
{
    // XXX: This needs to be dynamically read from lower level
    rssi -= -95;

    // in case the original rssi is even lower than -95 we cap it:
    if (rssi < 0) rssi = 0;

    return rssi;
}


/******************************************************************************
 *  RADIO CONTROL
 *****************************************************************************/

static bool stats_enable(radio_entry_t *radio_cfg, bool enable)
{
    int                 ret;
    INT radioIndex;

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radioIndex))
    {
        LOGE("Cannot get radio index for radio %s", radio_cfg->phy_name);
        return false;
    }

    ret = wifi_setRadioStatsEnable(radioIndex, enable);

    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to enable stats via HAL!!", radio_cfg->phy_name);
        return false;
    }

    return true;
}

static int auto_rssi_to_above_noise_floor(int rssi)
{
    // if rssi is absolute (negative value) convert to "above noise floor"
    // otherwise return as is
    if (rssi < 0)
    {
        return rssi_to_above_noise_floor(rssi);
    }
    return rssi;
}


/******************************************************************************
 *  CLIENT
 *****************************************************************************/

static void stats_client_record_free(stats_client_record_t *client_entry)
{
    if (client_entry != NULL)
    {
        free(client_entry);
    }
}

static stats_client_record_t* stats_client_record_alloc()
{
    return calloc(1, sizeof(stats_client_record_t));
}

static bool stats_client_fetch(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list,
        int                         radioIndex,
        int                         apIndex,
        char                       *apName,
        wifi_associated_dev3_t     *assoc_dev)
{
    stats_client_record_t *client_entry = NULL;
    mac_address_str_t mac_str;
    ULLONG handle = 0;
    int ret;

    client_entry = stats_client_record_alloc();

    // INFO
    client_entry->info.type = radio_cfg->type;
    memcpy(&client_entry->info.mac, assoc_dev->cli_MACAddress, sizeof(client_entry->info.mac));
    STRLCPY(client_entry->info.ifname, apName);
    STRLCPY(client_entry->info.essid, *essid);
    dpp_mac_to_str(assoc_dev->cli_MACAddress, mac_str);

    // STATS
    memcpy(&client_entry->dev3, assoc_dev, sizeof(client_entry->dev3));

    ret = wifi_getApAssociatedDeviceStats(
            apIndex,
            &assoc_dev->cli_MACAddress,
            &client_entry->stats,
            &handle);
    if (ret != RETURN_OK) goto err;

    client_entry->stats_cookie = handle;

out:
    ds_dlist_insert_tail(client_list, client_entry);
    return true;

err:
    LOG(WARNING, "%s: fetch error", __FUNCTION__);
    stats_client_record_free(client_entry);
    return false;
}


bool stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list)
{
    radio_essid_t ssid_name;
    wifi_associated_dev3_t *client_array;
    UINT client_num;
    int ret;
    int i;
    INT radio_index;
    INT ssid_radio_index;
    ULONG s, snum;
    char ssid_ifname[128];
    BOOL enabled = false;

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radio_index))
    {
        LOGE("%s: radio not found: %s", __func__, radio_cfg->phy_name);
        return false;
    }

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        return false;
    }

    for (s = 0; s < snum; s++)
    {
        ret = wifi_getSSIDEnable(s, &enabled);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get SSID enabled state for index %lu. Skipping", __func__, s);
            continue;
        }

        // Silently skip ifaces that are not enabled
        if (enabled == false) continue;

        memset(ssid_ifname, 0, sizeof(ssid_ifname));
        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get ap name for index %ld", __func__, s);
            continue;
        }

        // Silentely skip VAPs that are not controlled by OpenSync
        if (!vap_controlled(ssid_ifname)) continue;

        ret = wifi_getSSIDRadioIndex(s, &ssid_radio_index);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get radio index for SSID %s\n", __func__,
                    ssid_ifname);
            continue;
        }

        if (radio_index != ssid_radio_index)
        {
            continue;
        }

        ret = wifi_getSSIDNameStatus(s, ssid_name);
        if (ret != RETURN_OK)
        {
           LOGE("%s: cannot get SSID name status for %s", __func__, ssid_ifname);
           continue;
        }

        if (essid && strcmp(*essid, ssid_name))
        {
           continue;
        }

        client_array = NULL;
        client_num = 0;
        ret = wifi_getApAssociatedDeviceDiagnosticResult3(s, &client_array, &client_num);
        if (ret != RETURN_OK)
        {
            LOGW("%s %s %ld %s: fetch client list",
                 radio_cfg->phy_name, ssid_ifname, s, ssid_name);
            continue;
        }

        LOGT("%s %s %ld %s: fetch client list: %d clients",
             radio_cfg->phy_name, ssid_ifname, s, ssid_name, client_num);

        for (i = 0; i < (int)client_num; i++)
        {
            stats_client_fetch(
                    radio_cfg, &ssid_name, client_list,
                    radio_index, s, ssid_ifname, &client_array[i]);
        }

        free(client_array);
    }

    return true;
}

bool stats_clients_convert(
        radio_entry_t             *radio_cfg,
        stats_client_record_t     *data_new,
        stats_client_record_t     *data_old,
        dpp_client_record_t       *client_result)
{
    mac_address_str_t mac_str;
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
                mac_str, #X, (unsigned long long)A->X, \
                (unsigned long long)data_new->Y, (unsigned long long)data_old->Y, \
                (unsigned long long)GET_DELTA(Y)); \
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
        memset(&data_old->dev3, 0, sizeof(data_old->dev3));
    }

    ADD_DELTA(stats.bytes_tx,   stats.cli_tx_bytes);
    ADD_DELTA(stats.bytes_rx,   stats.cli_rx_bytes);
    ADD_DELTA(stats.frames_tx,  stats.cli_tx_frames);
    ADD_DELTA(stats.frames_rx,  stats.cli_rx_frames);
    ADD_DELTA(stats.retries_tx, stats.cli_tx_retries);
    ADD_DELTA(stats.retries_rx, stats.cli_rx_retries);
    ADD_DELTA(stats.errors_tx,  stats.cli_tx_errors);
    ADD_DELTA(stats.errors_rx,  stats.cli_rx_errors);

    client_result->stats.rssi = data_new->dev3.cli_SNR;
    LOG(TRACE, "Client %s stats %s=%d", mac_str, "stats.rssi", client_result->stats.rssi);

    /* 11ax compatible HAL implementation should provide an average tx/rx rates [mbps] that
     * are SU-normalized.
     */
    client_result->stats.rate_tx = data_new->stats.cli_tx_rate;
    client_result->stats.rate_rx = data_new->stats.cli_rx_rate;

    return true;
}


/******************************************************************************
 *  SURVEY
 *****************************************************************************/

bool stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        ds_dlist_t                 *survey_list)
{
    int i;
    int ret;
    int radioIndex = 0;
    stats_survey_record_t *survey_record;

    // phy_name
    if (!radio_entry_to_hal_radio_index(radio_cfg, &radioIndex))
    {
        return false;
    }

    // Mark requested channels
    survey_data_t survey_data;
    memset(&survey_data, 0, sizeof(survey_data));
    for (i = 0; i < (int)chan_num; i++)
    {
        survey_data.chan[i].ch_number = chan_list[i];
        survey_data.chan[i].ch_in_pool = true;
    }

    ret = wifi_getRadioChannelStats(radioIndex, survey_data.chan, chan_num);
    if (ret != RETURN_OK) return false;

    survey_data.num_chan = chan_num;
    survey_data.timestamp_ms = get_timestamp();

    // Assume that all were collected and stored into the array
    for (i = 0; i < (int)survey_data.num_chan; i++)
    {
        survey_record = stats_survey_record_alloc();
        if (survey_record == NULL)
        {
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
            survey_record->stats.survey_bss.chan_noise    = survey_data.chan[i].ch_noise;

            LOGT("Fetched %s %s %u survey "
                 "{active=%llu busy=%llu tx=%llu self=%llu rx=%llu ext=%llu noise=%d}",
                 radio_get_name_from_type(radio_cfg->type),
                 radio_get_scan_name_from_type(scan_type),
                 survey_record->info.chan,
                 (unsigned long long)survey_record->stats.survey_bss.chan_active,
                 (unsigned long long)survey_record->stats.survey_bss.chan_busy,
                 (unsigned long long)survey_record->stats.survey_bss.chan_tx,
                 (unsigned long long)survey_record->stats.survey_bss.chan_self,
                 (unsigned long long)survey_record->stats.survey_bss.chan_rx,
                 (unsigned long long)survey_record->stats.survey_bss.chan_busy_ext,
                 survey_record->stats.survey_bss.chan_noise);
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
            survey_record->stats.survey_obss.chan_noise    = survey_data.chan[i].ch_noise;

            LOGT("Fetched %s %s %u survey "
                 "{active=%u busy=%u tx=%u self=%u rx=%u ext=%u noise=%d}",
                 radio_get_name_from_type(radio_cfg->type),
                 radio_get_scan_name_from_type(scan_type),
                 survey_record->info.chan,
                 survey_record->stats.survey_obss.chan_active,
                 survey_record->stats.survey_obss.chan_busy,
                 survey_record->stats.survey_obss.chan_tx,
                 survey_record->stats.survey_obss.chan_self,
                 survey_record->stats.survey_obss.chan_rx,
                 survey_record->stats.survey_obss.chan_busy_ext,
                 survey_record->stats.survey_obss.chan_noise);
        }

        ds_dlist_insert_tail(survey_list, survey_record);
    }

    return true;
}

bool stats_survey_convert(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        stats_survey_record_t      *data_new,
        stats_survey_record_t      *data_old,
        dpp_survey_record_t        *survey_record)
{
    radio_type_t                    radio_type;

    if ((!data_new) || (!data_old) || (!survey_record))
    {
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
        stats_survey_bss_t     data;

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
            (unsigned long long)data.chan_active,
            (unsigned long long)data.chan_busy,
            (unsigned long long)data.chan_tx,
            (unsigned long long)data.chan_self,
            (unsigned long long)data.chan_rx,
            (unsigned long long)data.chan_busy_ext);

        // Repeat the measurement
        if (!data.chan_active) return false;

        survey_record->chan_busy     = PERCENT(data.chan_busy, data.chan_active);
        survey_record->chan_tx       = PERCENT(data.chan_tx, data.chan_active);
        survey_record->chan_rx       = PERCENT(data.chan_rx, data.chan_active);
        survey_record->chan_self     = PERCENT(data.chan_self, data.chan_active);
        survey_record->chan_busy_ext = PERCENT(data.chan_busy_ext, data.chan_active);
        survey_record->duration_ms   = data.chan_active / 1000;
        survey_record->chan_noise    = data_new->stats.survey_bss.chan_noise;
    }
    else /* OFF and FULL */
    {
        stats_survey_obss_t     data;

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
        survey_record->chan_noise    = data_new->stats.survey_obss.chan_noise;
    }

    return true;
}

static bool stats_scan_initiate(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time)
{
    int apIndex = -1;
    wifi_neighborScanMode_t scan_mode;
    uint32_t *c;
    uint32_t i;
    char buf[1024];
    char tmp[32];
    int ret;
    ULONG s, snum;
    char ssid_ifname[128];
    BOOL enabled;

    memset(ssid_ifname, 0, sizeof(ssid_ifname));
    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        return false;
    }

    for (s = 0; s < snum; s++)
    {
        ret = wifi_getSSIDEnable(s, &enabled);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get SSID enabled state for index %lu. Skipping", __func__, s);
            continue;
        }

        // Silently skip ifaces that are not enabled
        if (enabled == false) continue;

        memset(ssid_ifname, 0, sizeof(ssid_ifname));
        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get ap name for index %ld", __func__, s);
            continue;
        }

        // Silentely skip VAPs that are not controlled by OpenSync
        if (!vap_controlled(ssid_ifname)) continue;

        if (!strcmp(ssid_ifname, radio_cfg->if_name))
        {
            apIndex = s;
            break;
        }
    }

    if (apIndex == -1)
    {
        LOGE("%s: cannot find SSID index for %s", __func__, radio_cfg->if_name);
        return false;
    }

    if (scan_type == RADIO_SCAN_TYPE_ONCHAN)
    {
        scan_mode = WIFI_RADIO_SCAN_MODE_ONCHAN;
    } else
    {
        scan_mode = WIFI_RADIO_SCAN_MODE_OFFCHAN;
    }

    buf[0] = '\0';
    c = chan_list;
    for (i = 0; i < chan_num; i++)
    {
        sprintf(tmp, "%u", *c++);
        if (buf[0] != '\0')
        {
            strcat(buf, " ");
        }
        strcat(buf, tmp);
    }

    ret = wifi_startNeighborScan(apIndex, scan_mode, dwell_time, chan_num, chan_list);

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
    stats_scan_cb_t                *scan_cb;
    void                           *scan_ctx;
} stats_scan_request_t;

#define STATS_SCAN_RESULT_POLL_TIME       0.2
// Need to wait 20s for FULL chan results
#define STATS_SCAN_RESULT_POLL_TIMEOUT    100  // 100 * 0.2 = 20 sec

static wifi_neighbor_ap2_t* g_scan_results = NULL;
static uint32_t             g_scan_results_size;
static ev_timer             g_scan_result_timer;
static int32_t              g_scan_result_timeout;

static void stats_scan_result_timer_set(
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

static void stats_scan_results_fetch(EV_P_ ev_timer *w, int revents)
{
    int                          scan_status = false;
    stats_scan_request_t         *request_ctx = (stats_scan_request_t *) w->data;
    radio_entry_t                *radio_cfg = request_ctx->radio_cfg;
    radio_type_t                 radio_type = radio_cfg->type;
    radio_scan_type_t            scan_type = request_ctx->scan_type;
    int radio_index;
    bool ret;

    // The driver scans and adds results to the buffer specified.
    // Since we do no know when scanning is finished, we need to poll for info.
    // We poll in steps of 250ms. Max waiting time is 5s.

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radio_index))
    {
        goto exit;
    }

    free(g_scan_results);
    g_scan_results = NULL;

#ifdef WIFI_HAL_VERSION_3_PHASE2
    ret = wifi_getNeighboringWiFiStatus(radio_index, false, &g_scan_results, &g_scan_results_size);
#else
    ret = wifi_getNeighboringWiFiStatus(radio_index, &g_scan_results, &g_scan_results_size);
#endif
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
    stats_scan_result_timer_set(w, false);
    g_scan_result_timeout = STATS_SCAN_RESULT_POLL_TIMEOUT;

clean:
    // Notify upper layer about scan status (blocking)
    if (request_ctx->scan_cb)
    {
        request_ctx->scan_cb(request_ctx->scan_ctx, scan_status);
    }

restart_timer:
    return;
}

bool stats_scan_start(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        stats_scan_cb_t            *scan_cb,
        void                       *scan_ctx)
{
    if (!stats_scan_initiate(
                radio_cfg,
                chan_list,
                chan_num,
                scan_type,
                dwell_time))
    {
        return false;
    }

    static stats_scan_request_t scan_request;  /* TODO: unify sm_scan_request */

    memset (&scan_request, 0, sizeof(scan_request));
    scan_request.radio_cfg  = radio_cfg;
    scan_request.scan_type  = scan_type;
    scan_request.scan_cb    = scan_cb;
    scan_request.scan_ctx   = scan_ctx;

    // Start result polling timer
    ev_init (&g_scan_result_timer, stats_scan_results_fetch);
    g_scan_result_timer.repeat =  STATS_SCAN_RESULT_POLL_TIME;
    g_scan_result_timer.data = &scan_request;
    stats_scan_result_timer_set(&g_scan_result_timer, true);
    g_scan_result_timeout = STATS_SCAN_RESULT_POLL_TIMEOUT;

    return true;
}

bool stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    stats_scan_result_timer_set(&g_scan_result_timer, false);
    return true;
}

static void stats_scan_hal_to_dpp_record(
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
    //uint64_t tsf;  // not available
}

bool stats_scan_hal_to_dpp_record_array(
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

        entry = &scan_records[*scan_result_qty];
        (*scan_result_qty)++;
        stats_scan_hal_to_dpp_record(hal, radio_type, entry);
    }

    return true;
}

static bool stats_scan_extract_neighbors_from_ssids(
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

bool stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results)
{
    dpp_neighbor_record_t scan_records[STATS_SCAN_MAX_RECORDS];
    int scan_result_qty = 0;
    radio_type_t radio_type;
    bool ret;

    if (scan_results == NULL)
    {
        return false;
    }

    radio_type = radio_cfg->type;

    ret = stats_scan_hal_to_dpp_record_array(
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
    bool success = stats_scan_extract_neighbors_from_ssids (
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

bool stats_capacity_get(
        radio_entry_t              *radio_cfg,
        stats_capacity_data_t      *capacity_result)
{
    int ret;
    INT radio_index;
    INT ssid_radio_index;
    ULONG s, snum;
    char ssid_ifname[128];
    BOOL enabled;

    memset(capacity_result, 0, sizeof(*capacity_result));

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radio_index))
    {
        LOGE("%s: radio not found: %s", __func__, radio_cfg->phy_name);
        return false;
    }

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        return false;
    }

    for (s = 0; s < snum; s++)
    {

        ret = wifi_getSSIDEnable(s, &enabled);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get SSID enabled state for index %lu. Skipping", __func__, s);
            continue;
        }

        // Silently skip ifaces that are not enabled
        if (enabled == false) continue;
        memset(ssid_ifname, 0, sizeof(ssid_ifname));

        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get ap name for index %ld", __func__, s);
            continue;
        }

        // Silentely skip VAPs that are not controlled by OpenSync
        if (!vap_controlled(ssid_ifname)) continue;

        ret = wifi_getSSIDRadioIndex(s, &ssid_radio_index);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get radio index for SSID %s\n", __func__,
                    ssid_ifname);
            continue;
        }

        if (radio_index != ssid_radio_index)
        {
            continue;
        }

        wifi_ssidTrafficStats2_t ssid_stats;
        ret = wifi_getSSIDTrafficStats2(s, &ssid_stats);
        if (ret != RETURN_OK) return false;
        capacity_result->bytes_tx += ssid_stats.ssid_BytesSent;
    }

    wifi_channelStats_t chan_stats;
    chan_stats.ch_number = radio_cfg->chan;
    // num 0 means onchan (bss)
    ret = wifi_getRadioChannelStats(radio_index, &chan_stats, 0);
    if (ret != RETURN_OK) return false;
    capacity_result->chan_active = chan_stats.ch_utilization_total;
    capacity_result->chan_tx = chan_stats.ch_utilization_busy_tx;

    // capacity queue util not used anymore

    return true;
}

static
radio_entry_t* target_radio_scan_config_map(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    static radio_entry_t            radio_cfg_ctx[RADIO_MAX_DEVICE_QTY][RADIO_SCAN_MAX_TYPE_QTY];
    int                             radio_index;
    int                             scan_index;

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radio_index))
    {
        return NULL;
    }

    scan_index = radio_get_scan_index_from_type(scan_type);
    if (scan_index < 0)
    {
        return NULL;
    }

    memcpy(&radio_cfg_ctx[radio_index][scan_index], radio_cfg, sizeof(*radio_cfg));
    STRSCPY(radio_cfg_ctx[radio_index][scan_index].if_name,
            target_map_ifname(radio_cfg->if_name));

    return &radio_cfg_ctx[radio_index][scan_index];
}

static
radio_entry_t* target_radio_config_map(
        radio_entry_t    *radio_cfg)
{
    static radio_entry_t            radio_cfg_ctx[RADIO_MAX_DEVICE_QTY];
    int                             radio_index;

    if (!radio_entry_to_hal_radio_index(radio_cfg, &radio_index))
    {
        return NULL;
    }

    memcpy(&radio_cfg_ctx[radio_index], radio_cfg, sizeof(*radio_cfg));
    STRSCPY(radio_cfg_ctx[radio_index].if_name,
            target_map_ifname(radio_cfg->if_name));

    return &radio_cfg_ctx[radio_index];
}


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

/******************************************************************************
 *  INTERFACE definitions
 *****************************************************************************/

bool target_is_interface_ready(char *if_name)
{
    bool rc;

    rc = os_nif_is_interface_ready(target_map_ifname(if_name));
    if (rc == false)
    {
        return false;
    }

    return true;
}

bool target_is_radio_interface_ready(char *phy_name)
{
#ifdef QCA_WIFI
    bool rc;

    rc = os_nif_is_interface_ready(phy_name);
    if (rc == false)
    {
        return false;
    }
#endif /* QCA_WIFI */

    return true;
}


/******************************************************************************
 *  RADIO definitions
 *****************************************************************************/

bool target_radio_tx_stats_enable(
        radio_entry_t              *radio_cfg,
        bool                        enable)
{
    radio_entry_t *radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    return stats_enable(radio_cfg_ctx, enable);

    return true;
}

bool target_radio_fast_scan_enable(
        radio_entry_t              *radio_cfg,
        ifname_t                    if_name)
{
    // Enabled at the same time as tx_stats
    return true;
}


/******************************************************************************
 *  CLIENT definitions
 *****************************************************************************/

target_client_record_t* target_client_record_alloc()
{
    return stats_client_record_alloc();
}

void target_client_record_free(target_client_record_t *record)
{
    stats_client_record_free(record);
}

bool target_stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx)
{
    bool status = true;

    radio_entry_t *radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL)
    {
        status = false;
        goto exit;
    }

    bool ret;

    ret = stats_clients_get(radio_cfg_ctx, essid, client_list);
    if (!ret)
    {
        status = false;
    }

exit:
    return (*client_cb)(client_list, client_ctx, status);
}

bool target_stats_clients_convert(
        radio_entry_t              *radio_cfg,
        target_client_record_t     *data_new,
        target_client_record_t     *data_old,
        dpp_client_record_t        *client_result)
{
    radio_entry_t *radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    return stats_clients_convert(
            radio_cfg_ctx,
            data_new,
            data_old,
            client_result);
}


/******************************************************************************
 *  SURVEY definitions
 *****************************************************************************/

target_survey_record_t* target_survey_record_alloc()
{
    return stats_survey_record_alloc();
}

void target_survey_record_free(target_survey_record_t *result)
{
    stats_survey_record_free(result);
}

bool target_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        target_stats_survey_cb_t   *survey_cb,
        ds_dlist_t                 *survey_list,
        void                       *survey_ctx)
{
    radio_entry_t *radio_cfg_ctx = NULL;
    bool ret;
    bool status = true;

    radio_cfg_ctx = target_radio_scan_config_map(radio_cfg, scan_type);
    if (radio_cfg_ctx == NULL)
    {
        status = false;
        goto exit;
    }

    ret = stats_survey_get(
                radio_cfg_ctx,
                chan_list,
                chan_num,
                scan_type,
                survey_list);
    if (!ret)
    {
        status = false;
    }

exit:
    return (*survey_cb)(survey_list, survey_ctx, status);
}

bool target_stats_survey_convert(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        target_survey_record_t     *data_new,
        target_survey_record_t     *data_old,
        dpp_survey_record_t        *survey_record)
{
    radio_entry_t *radio_cfg_ctx = NULL;
    bool ret;

    radio_cfg_ctx = target_radio_scan_config_map(radio_cfg, scan_type);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    ret = stats_survey_convert(
                radio_cfg_ctx,
                scan_type,
                data_new,
                data_old,
                survey_record);
    if (!ret)
    {
        return false;
    }

    return true;
}


/******************************************************************************
 *  NEIGHBORS definitions
 *****************************************************************************/

bool target_stats_scan_start(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        target_scan_cb_t           *scan_cb,
        void                       *scan_ctx)
{
    radio_entry_t *radio_cfg_ctx = NULL;
    bool ret;

    radio_cfg_ctx = target_radio_scan_config_map(radio_cfg, scan_type);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    ret = stats_scan_start(
            radio_cfg_ctx,
            chan_list,
            chan_num,
            scan_type,
            dwell_time,
            scan_cb,
            scan_ctx);
    if (!ret)
    {
        return false;
    }

    return true;
}

bool target_stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    return stats_scan_stop(radio_cfg, scan_type);
}

bool target_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results)
{
    radio_entry_t *radio_cfg_ctx = NULL;
    bool ret;

    radio_cfg_ctx = target_radio_scan_config_map(radio_cfg, scan_type);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    ret = stats_scan_get(
            radio_cfg_ctx,
            chan_list,
            chan_num,
            scan_type,
            scan_results);
    if (!ret)
    {
        return false;
    }

    return true;
}


/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

// not supported


/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/

bool target_stats_capacity_enable(
        radio_entry_t              *radio_cfg,
        bool                        enabled)
{
#if defined USE_CAPACITY_QUEUE_STATS
#error CAPACITY_QUEUE_STATS obsolete
#endif
    return true;
}

bool target_stats_capacity_get(
        radio_entry_t              *radio_cfg,
        target_capacity_data_t     *capacity_new)
{
#if defined USE_CAPACITY_QUEUE_STATS
    radio_entry_t *radio_cfg_ctx = NULL;
    bool ret;

    radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL)
    {
        return false;
    }

    ret = stats_capacity_get(radio_cfg_ctx, capacity_new);
    if (!ret)
    {
        LOG(ERR, "Processing %s capacity",
                 radio_get_name_from_type(radio_cfg->type));
        return false;
    }
#endif

    return true;
}

bool target_stats_capacity_convert(
        target_capacity_data_t     *capacity_new,
        target_capacity_data_t     *capacity_old,
        dpp_capacity_record_t      *capacity_entry)
{
#if defined USE_CAPACITY_QUEUE_STATS
    target_capacity_data_t  capacity_delta;
    int32_t                 queue_index = 0;

    // Calculate time deltas and derive percentage per sample

    memset(&capacity_delta, 0, sizeof(capacity_delta));

#define STATS_DELTA(n, o) (n - o)
    capacity_delta.chan_active =
        STATS_DELTA(
                capacity_new->chan_active,
                capacity_old->chan_active);

    capacity_delta.chan_tx =
        STATS_DELTA(
                capacity_new->chan_tx,
                capacity_old->chan_tx);

    for (queue_index = 0; queue_index < RADIO_QUEUE_MAX_QTY; queue_index++)
    {
        capacity_delta.queue[queue_index] =
            STATS_DELTA(
                    capacity_new->queue[queue_index],
                    capacity_old->queue[queue_index]);
    }

#define STATS_PERCENT(v1, v2) \
        (v2 > 0 ? (v1*100/v2) : 0)

    capacity_entry->busy_tx =
        STATS_PERCENT(
                capacity_delta.chan_tx,
                capacity_delta.chan_active);

    capacity_entry->bytes_tx =
        STATS_DELTA(
                capacity_new->bytes_tx,
                capacity_old->bytes_tx);

    capacity_entry->samples =
        STATS_DELTA(
                capacity_new->samples,
                capacity_old->samples);

    for (queue_index = 0; queue_index < RADIO_QUEUE_MAX_QTY; queue_index++)
    {
        capacity_entry->queue[queue_index] =
            STATS_PERCENT(
                    capacity_delta.queue[queue_index],
                    capacity_entry->samples);
    }
#endif

    return true;
}
