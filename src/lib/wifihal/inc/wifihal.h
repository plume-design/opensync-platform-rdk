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
 * wifihal.h
 *
 * RDKB Platform - Wifi HAL
 */

#ifndef WIFIHAL_H_INCLUDED
#define WIFIHAL_H_INCLUDED

#include <ev.h>
#include <mesh/meshsync_msgs.h>
#include "ds_tree.h"
#include "ds_dlist.h"
#include "schema.h"
#include "dpp_types.h"
#include "dpp_client.h"
#include "dpp_survey.h"
#include "dpp_neighbor.h"

#ifndef __WIFI_HAL_H__
#include "ccsp/wifi_hal.h"
#endif

/*****************************************************************************/

#define WIFIHAL_MAX_BUFFER          64
#define WIFIHAL_MAX_MACSTR          18

// When defined a warning is printed when wifi_hal calls take too long
#define WIFIHAL_TIME_CALLS
#define WIFIHAL_STD_TIME            2

#define WIFIHAL_KEY_DEFAULT         "key"

typedef enum
{
    WIFIHAL_CLOUD_MODE_UNKNOWN  = 0,
    WIFIHAL_CLOUD_MODE_MONITOR  = 1,
    WIFIHAL_CLOUD_MODE_FULL     = 2,
} wifihal_cloud_mode_t;

typedef enum
{
    WIFIHAL_CHAN_MODE_MANUAL    = 0,
    WIFIHAL_CHAN_MODE_AUTO      = 1,
    WIFIHAL_CHAN_MODE_CLOUD     = 2,
} wifihal_chan_mode_t;

typedef enum
{
    WIFIHAL_SYNC_MGR_WM         = 0,
    WIFIHAL_SYNC_MGR_NM,
    WIFIHAL_SYNC_MGR_CM,
} wifihal_sync_mgr_t;

typedef enum
{
    WIFIHAL_MACLEARN_TYPE_ETH   = 0,
    WIFIHAL_MACLEARN_TYPE_MOCA,
} wifihal_maclearn_type_t;

/*****************************************************************************/

typedef struct
{
    int                 index;
    char                ifname[WIFIHAL_MAX_BUFFER];
    char                band[WIFIHAL_MAX_BUFFER];
    bool                auto_chan;
    bool                csa_in_progress;
    int                 channel;

    bool                enabled;
    bool                channel_sync;

    ds_dlist_t          ssids;
    ds_dlist_node_t     dsl_node;
} wifihal_radio_t;

typedef struct
{
    int                 index;
    char                ifname[WIFIHAL_MAX_BUFFER];
    bool                enabled;
    wifihal_radio_t     *radio;
#ifdef WPA_CLIENTS
    int                 wpafd;
    ev_io               iowatcher;
    struct wpa_ctrl     *wpactrl;
#endif /* WPA_CLIENTS */
    ds_tree_t           keys;
    ds_dlist_node_t     dsl_node;
} wifihal_ssid_t;

typedef struct
{
    char                id[WIFIHAL_MAX_BUFFER];
    char                psk[WIFIHAL_MAX_BUFFER];
    char                oftag[WIFIHAL_MAX_BUFFER];

    ds_tree_node_t      dst_node;
} wifihal_key_t;

#define WIFIHAL_SURVEY_CHAN_MAX 64
typedef struct
{
    uint64_t            timestamp_ms;
    uint32_t            num_chan;
    wifi_channelStats_t chan[WIFIHAL_SURVEY_CHAN_MAX];
} wifihal_survey_data_t;

// on-channel survey
typedef struct
{
    uint64_t            chan_active;
    uint64_t            chan_busy;
    uint64_t            chan_busy_ext;
    uint64_t            chan_self;
    uint64_t            chan_rx;
    uint64_t            chan_tx;
} wifihal_survey_bss_t;

// off-channel survey
typedef struct
{
    uint32_t            chan_active;
    uint32_t            chan_busy;
    uint32_t            chan_busy_ext;
    uint32_t            chan_self;
    uint32_t            chan_rx;
    uint32_t            chan_tx;
} wifihal_survey_obss_t;

typedef struct
{
    // General survey data (upper layer cache key)
    dpp_survey_info_t           info;

    // Target specific survey data
    union {
        wifihal_survey_bss_t    survey_bss;
        wifihal_survey_obss_t   survey_obss;
    } stats;

    // Linked list of survey data
    ds_dlist_node_t             node;
} wifihal_survey_record_t;

static inline
wifihal_survey_record_t* wifihal_survey_record_alloc()
{
    wifihal_survey_record_t *record = NULL;

    record = malloc(sizeof(wifihal_survey_record_t));
    if (record != NULL) {
        memset(record, 0, sizeof(wifihal_survey_record_t));
    }

    return record;
}

static inline
void wifihal_survey_record_free(wifihal_survey_record_t *record)
{
    if (record != NULL) {
        free(record);
    }
}

typedef struct
{
    uint64_t            chan_active;
    uint64_t            chan_tx;
    uint64_t            bytes_tx;
    uint64_t            samples;                     // obsolete
    uint64_t            queue[RADIO_QUEUE_MAX_QTY];  // obsolete
} wifihal_capacity_data_t;

// This needs to be equal or grater that PS_UAPI or target !!!
#define WIFIHAL_STATS_CCK_LONG_QTY      (4)
#define WIFIHAL_STATS_CCK_SHORT_QTY     (3)
#define WIFIHAL_STATS_CCK_QTY \
    (WIFIHAL_STATS_CCK_LONG_QTY + \
     WIFIHAL_STATS_CCK_SHORT_QTY)

#define WIFIHAL_STATS_OFDM_QTY          (8)
#define WIFIHAL_STATS_LEGACY_RECORDS \
    (WIFIHAL_STATS_OFDM_QTY + \
     WIFIHAL_STATS_CCK_QTY)

#define WIFIHAL_STATS_MSC_QTY           (10)
#define WIFIHAL_STATS_NSS_QTY           (8)
#define WIFIHAL_STATS_HT_RECORDS  \
    (CLIENT_MAX_RADIO_WIDTH_QTY * \
     WIFIHAL_STATS_MSC_QTY * \
     WIFIHAL_STATS_NSS_QTY)

#define WIFIHAL_STATS_RECORDS  \
    (WIFIHAL_STATS_LEGACY_RECORDS + \
     WIFIHAL_STATS_HT_RECORDS)


typedef struct
{
    // Client general data
    dpp_client_info_t               info;
    // Target specific client data
    wifi_associated_dev_stats_t     stats;
    wifi_associated_dev2_t          dev2;
    wifi_associated_dev_rate_info_rx_stats_t stats_rx[WIFIHAL_STATS_RECORDS];
    wifi_associated_dev_rate_info_tx_stats_t stats_tx[WIFIHAL_STATS_RECORDS];
    wifi_associated_dev_tid_stats_t tid_stats;
    unsigned                        num_rx;
    unsigned                        num_tx;
    uint64_t                        stats_cookie;
    ds_dlist_node_t                 node;
} wifihal_client_record_t;

extern struct ev_loop   *wifihal_evloop;

/*****************************************************************************/

extern bool                 wifihal_init(struct ev_loop *loop, bool wifi);
extern bool                 wifihal_cleanup(void);

extern bool                 wifihal_config_init(void);
extern bool                 wifihal_config_cleanup(void);

extern bool                 wifihal_redirector_update(const char *addr);
extern bool                 wifihal_device_config_init(void);
extern bool                 wifihal_device_config_register(void *devconfig_cb);

/*****************************************************************************/

extern ds_dlist_t *         wifihal_get_radios(void);
extern wifihal_radio_t *    wifihal_radio_by_index(int index);
extern wifihal_radio_t *    wifihal_radio_by_ifname(const char *ifname);
extern wifihal_ssid_t *     wifihal_ssid_by_index(int index);
extern wifihal_ssid_t *     wifihal_ssid_by_ifname(const char *ifname);
extern wifihal_ssid_t *     wifihal_ssid_by_iowatcher(ev_io *watcher);

/*****************************************************************************/

typedef void wifihal_radio_state_cb_t(
                struct schema_Wifi_Radio_State *rstate,
                schema_filter_t *filter);

typedef void wifihal_radio_config_cb_t(
                struct schema_Wifi_Radio_Config *rconf,
                schema_filter_t *filter);

extern bool                 wifihal_radio_state_register(char *ifname,
                                    wifihal_radio_state_cb_t *radio_state_cb);
extern bool                 wifihal_radio_config_register(const char *ifname,
                                    void *rconf_cb);
extern bool                 wifihal_radio_config_get(const char *ifname,
                                    struct schema_Wifi_Radio_Config *rconf);
extern bool                 wifihal_radio_state_get(const char *ifname,
                                    struct schema_Wifi_Radio_State *rstate,
                                    schema_filter_t *filter);
extern bool                 wifihal_radio_state_update(wifihal_radio_t *radio);
extern bool                 wifihal_radio_update(const char *ifname,
                                    struct schema_Wifi_Radio_Config *rconf);
extern bool                 wifihal_radio_updated(wifihal_radio_t *radio);
extern bool                 wifihal_radio_state_updated(wifihal_radio_t *radio);
extern bool                 wifihal_radio_all_updated(void);

/*****************************************************************************/

typedef void wifihal_vif_state_cb_t(
                struct schema_Wifi_VIF_State *vstate,
                schema_filter_t *filter);

typedef void wifihal_vif_config_cb_t(
                struct schema_Wifi_VIF_Config *vconf,
                schema_filter_t *filter);

extern bool                 wifihal_vif_state_register(char *ifname,
                                    wifihal_vif_state_cb_t *vif_state_cb);
extern bool                 wifihal_vif_config_register(const char *ifname,
                                    void *vconf_cb);
extern bool                 wifihal_vif_config_get(const char *ifname,
                                    struct schema_Wifi_VIF_Config *vconf);
extern bool                 wifihal_vif_state_get(const char *ifname,
                                    struct schema_Wifi_VIF_State *vstate,
                                    schema_filter_t *filter);
extern bool                 wifihal_vif_state_update(wifihal_ssid_t *ssid);
extern bool                 wifihal_vif_update(const char *ifname,
                                    struct schema_Wifi_VIF_Config *vconf);
extern bool                 wifihal_vif_conf_updated(wifihal_ssid_t *ssid);
extern bool                 wifihal_vif_conf_all_updated(wifihal_radio_t *radio);
extern bool                 wifihal_vif_state_updated(wifihal_ssid_t *ssid);
extern bool                 wifihal_vif_is_enabled(wifihal_ssid_t *ssid);

/*****************************************************************************/

extern bool                 wifihal_security_same(struct schema_Wifi_VIF_Config *vconf1,
                                                  struct schema_Wifi_VIF_Config *vconf2);
extern bool                 wifihal_security_to_config(wifihal_ssid_t *ssid,
                                    struct schema_Wifi_VIF_Config *vconf);
extern bool                 wifihal_security_to_syncmsg(struct schema_Wifi_VIF_Config *vconf,
                                    MeshWifiAPSecurity *dest);
extern void                 wifihal_security_key_cleanup(wifihal_ssid_t *ssid);
extern bool                 wifihal_security_key_from_conf(wifihal_ssid_t *ssid,
                                    struct schema_Wifi_VIF_Config *vconf);
extern wifihal_key_t *      wifihal_security_key_find_by_psk(wifihal_ssid_t *ssid, char *psk);
extern wifihal_key_t *      wifihal_security_key_find_by_id(wifihal_ssid_t *ssid, char *id);

/*****************************************************************************/

extern bool                 wifihal_acl_to_config(wifihal_ssid_t *ssid,
                                    struct schema_Wifi_VIF_Config *vconf);
extern bool                 wifihal_acl_from_config(wifihal_ssid_t *ssid,
                                    struct schema_Wifi_VIF_Config *vconf);

/*****************************************************************************/

extern bool                 wifihal_clients_init(void *clients_update_cb);
extern bool                 wifihal_clients_cleanup(void);
extern void                 wifihal_clients_connection(wifihal_ssid_t *ssid,
                                    char *mac, char *key_id);
extern void                 wifihal_clients_disconnection(wifihal_ssid_t *ssid,
                                    char *mac);

/*****************************************************************************/

#ifdef WPA_CLIENTS
extern bool                 wifihal_clients_wpa_init(void);
extern bool                 wifihal_clients_wpa_cleanup(void);
extern bool                 wifihal_wpactrl_request(wifihal_ssid_t *ssid,
                                    const char *cmd,
                                    char *reply, size_t *reply_len);
#else  /* not WPA_CLIENTS */
extern bool                 wifihal_clients_hal_init(void);
extern bool                 wifihal_clients_hal_cleanup(void);
#endif /* not WPA_CLIENTS */

/*****************************************************************************/

extern bool                 wifihal_sync_init(wifihal_sync_mgr_t mgr);
extern bool                 wifihal_sync_cleanup(void);
extern bool                 wifihal_sync_connected(void);
extern bool                 wifihal_sync_send_ssid_change(wifihal_ssid_t *ssid,
                                    const char *new_ssid);
extern bool                 wifihal_sync_send_security_change(wifihal_ssid_t *ssid,
                                    MeshWifiAPSecurity *sec);
extern bool                 wifihal_sync_send_status(wifihal_cloud_mode_t mode);

/*****************************************************************************/

extern bool                 wifihal_cloud_mode_init(void);
extern bool                 wifihal_cloud_mode_sync(void);
extern bool                 wifihal_cloud_mode_set(wifihal_cloud_mode_t mode);
extern wifihal_cloud_mode_t wifihal_cloud_mode_get(void);
extern wifihal_chan_mode_t  wifihal_cloud_mode_get_chan_mode(wifihal_radio_t *radio);

/*****************************************************************************/

extern bool                 wifihal_health_init(void);
extern bool                 wifihal_health_cleanup(void);
extern void                 wifihal_fatal_restart(bool block, char *reason);

/*****************************************************************************/

extern bool                 wifihal_maclearn_init(void *maclearn_cb);
extern bool                 wifihal_maclearn_cleanup(void);
extern bool                 wifihal_maclearn_update(wifihal_maclearn_type_t type,
                                                    const char *mac,
                                                    bool connected);

/*****************************************************************************/

extern bool                 wifihal_stats_enable(radio_entry_t *radio_cfg, bool enable);

void wifihal_client_record_free(wifihal_client_record_t *client_entry);
wifihal_client_record_t* wifihal_client_record_alloc();


bool wifihal_stats_clients_get(
        radio_entry_t              *radio_cfg,
        radio_essid_t              *essid,
        ds_dlist_t                 *client_list);

bool wifihal_stats_clients_convert(
        radio_entry_t              *radio_cfg,
        wifihal_client_record_t    *data_new,
        wifihal_client_record_t    *data_old,
        dpp_client_record_t        *client_result);

bool wifihal_stats_survey_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        ds_dlist_t                 *survey_list);

bool wifihal_stats_survey_convert(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type,
        wifihal_survey_record_t    *data_new,
        wifihal_survey_record_t    *data_old,
        dpp_survey_record_t        *survey_record);

typedef bool wifihal_scan_cb_t(
        void                       *scan_ctx,
        int                         status);

bool wifihal_stats_scan_start(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        int32_t                     dwell_time,
        wifihal_scan_cb_t          *scan_cb,
        void                       *scan_ctx);

bool wifihal_stats_scan_stop(
        radio_entry_t              *radio_cfg,
        radio_scan_type_t           scan_type);

bool wifihal_stats_scan_get(
        radio_entry_t              *radio_cfg,
        uint32_t                   *chan_list,
        uint32_t                    chan_num,
        radio_scan_type_t           scan_type,
        dpp_neighbor_report_data_t *scan_results);

bool wifihal_stats_capacity_get(
        radio_entry_t              *radio_cfg,
        wifihal_capacity_data_t    *capacity_result);

#ifdef WIFIHAL_TIME_CALLS
extern ev_tstamp  __wifihal_tstamp;

#define WIFIHAL_TM_START() \
    do { \
        __wifihal_tstamp = ev_time(); \
    } while(0)

#define WIFIHAL_TM_STOP(ret, x, fmt, ...) \
    do { \
        ev_tstamp __tsnow = ev_time(); \
        ev_tstamp __tsdiff = __tsnow - __wifihal_tstamp; \
        if (ret != RETURN_OK || __tsdiff > x) \
            LOGW("[WIFI_HAL TM] Took %0.2fs: " fmt " = %d", __tsdiff, ## __VA_ARGS__, ret); \
    } while(0)

#define WIFIHAL_TM_STOP_NORET(x, fmt, ...) \
    do { \
        ev_tstamp __tsnow = ev_time(); \
        ev_tstamp __tsdiff = __tsnow - __wifihal_tstamp; \
        if (__tsdiff > x) \
            LOGW("[WIFI_HAL TM] Took %0.2fs: " fmt, __tsdiff, ## __VA_ARGS__); \
    } while(0)

#else  /* not WIFIHAL_TIME_CALLS */

// no-op implementations
#define WIFIHAL_TM_START()
#define WIFIHAL_TM_STOP(ret, x, fmt, ...)

#endif /* not WIFIHAL_TIME_CALLS */


#endif /* WIFIHAL_H_INCLUDED */
