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
#include "os_nif.h"
#include "log.h"

#include "target.h"


#define MODULE_ID LOG_MODULE_ID_OSA


/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/

static
radio_entry_t* target_radio_scan_config_map(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    static radio_entry_t            radio_cfg_ctx[RADIO_MAX_DEVICE_QTY][RADIO_SCAN_MAX_TYPE_QTY];
    int                             radio_index;
    int                             scan_index;

    radio_index = radio_get_index_from_type(radio_cfg->type);
    if (radio_index < 0) {
        return NULL;
    }

    scan_index = radio_get_scan_index_from_type(scan_type);
    if (scan_index < 0) {
        return NULL;
    }

    memcpy(&radio_cfg_ctx[radio_index][scan_index], radio_cfg, sizeof(*radio_cfg));
    strncpy(radio_cfg_ctx[radio_index][scan_index].if_name,
            target_map_ifname(radio_cfg->if_name),
            sizeof(radio_cfg_ctx[radio_index][scan_index].if_name));

    return &radio_cfg_ctx[radio_index][scan_index];
}

static
radio_entry_t* target_radio_config_map(
        radio_entry_t    *radio_cfg)
{
    static radio_entry_t            radio_cfg_ctx[RADIO_MAX_DEVICE_QTY];
    int                             radio_index;

    radio_index = radio_get_index_from_type(radio_cfg->type);
    if (radio_index < 0) {
        return NULL;
    }

    memcpy(&radio_cfg_ctx[radio_index], radio_cfg, sizeof(*radio_cfg));
    strncpy(radio_cfg_ctx[radio_index].if_name,
            target_map_ifname(radio_cfg->if_name),
            sizeof(radio_cfg_ctx[radio_index].if_name));

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
    if (rc == false) {
        return false;
    }

    return true;
}

bool target_is_radio_interface_ready(char *phy_name)
{
#ifdef QCA_WIFI
    bool rc;

    rc = os_nif_is_interface_ready(phy_name);
    if (rc == false) {
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
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    return wifihal_stats_enable(radio_cfg_ctx, enable);

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
    return wifihal_client_record_alloc();
}

void target_client_record_free(target_client_record_t *record)
{
    wifihal_client_record_free(record);
}

bool target_stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        target_stats_clients_cb_t  *client_cb,
        ds_dlist_t                 *client_list,
        void                       *client_ctx)
{
    radio_entry_t *radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    bool ret;

    ret = wifihal_stats_clients_get(radio_cfg_ctx, essid, client_list);
    if (!ret) {
        return false;
    }

    return (*client_cb)(client_list, client_ctx, true);
}

bool target_stats_clients_convert(
        radio_entry_t              *radio_cfg,
        target_client_record_t     *data_new,
        target_client_record_t     *data_old,
        dpp_client_record_t        *client_result)
{
    radio_entry_t *radio_cfg_ctx = target_radio_config_map(radio_cfg);
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    return wifihal_stats_clients_convert(
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
    return wifihal_survey_record_alloc();
}

void target_survey_record_free(target_survey_record_t *result)
{
    wifihal_survey_record_free(result);
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

    radio_cfg_ctx = target_radio_scan_config_map(radio_cfg, scan_type);
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    ret = wifihal_stats_survey_get(
                radio_cfg_ctx,
                chan_list,
                chan_num,
                scan_type,
                survey_list);
    if (!ret) {
        return false;
    }

    (*survey_cb)(survey_list, survey_ctx, true);

    return true;
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
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    ret = wifihal_stats_survey_convert(
                radio_cfg_ctx,
                scan_type,
                data_new,
                data_old,
                survey_record);
    if (!ret) {
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
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    ret = wifihal_stats_scan_start(
            radio_cfg_ctx,
            chan_list,
            chan_num,
            scan_type,
            dwell_time,
            scan_cb,
            scan_ctx);
    if (!ret) {
        return false;
    }

    return true;
}

bool target_stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type)
{
    return wifihal_stats_scan_stop(radio_cfg, scan_type);
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
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    ret = wifihal_stats_scan_get(
            radio_cfg_ctx,
            chan_list,
            chan_num,
            scan_type,
            scan_results);
    if (!ret) {
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
    if (radio_cfg_ctx == NULL) {
        return false;
    }

    ret = wifihal_stats_capacity_get(radio_cfg_ctx, capacity_new);
    if (!ret) {
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
