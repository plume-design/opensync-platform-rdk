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

#ifndef TARGET_INTERNAL_H_INCLUDED
#define TARGET_INTERNAL_H_INCLUDED

#include <stdbool.h>

#include "schema.h"
#include "dpp_types.h"
#include "dpp_client.h"
#include "dpp_survey.h"
#include "dpp_neighbor.h"
#include "osn_dhcp.h"

#ifndef CONFIG_RDK_DISABLE_SYNC
#include <mesh/meshsync_msgs.h>  // needed only by sync_send_security_change()
#endif

#ifndef __WIFI_HAL_H__
#include "ccsp/wifi_hal.h"
#endif

#define WIFIHAL_MAX_BUFFER          64
#define WIFIHAL_MAX_MACSTR          18

#define MAC_ADDR_FMT "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx"
#define MAC_ADDR_UNPACK(addr) addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

typedef enum
{
    SYNC_MGR_WM         = 0,
    SYNC_MGR_NM,
    SYNC_MGR_CM,
} sync_mgr_t;

typedef enum
{
    RADIO_CLOUD_MODE_UNKNOWN  = 0,
    RADIO_CLOUD_MODE_MONITOR  = 1,
    RADIO_CLOUD_MODE_FULL     = 2,
} radio_cloud_mode_t;


// This needs to be equal or greater than PS_UAPI or target !!!
#define STATS_CCK_LONG_QTY      (4)
#define STATS_CCK_SHORT_QTY     (3)
#define STATS_CCK_QTY \
    (STATS_CCK_LONG_QTY + \
     STATS_CCK_SHORT_QTY)

#define STATS_OFDM_QTY          (8)
#define STATS_LEGACY_RECORDS \
    (STATS_OFDM_QTY + \
     STATS_CCK_QTY)

#define STATS_MSC_QTY           (10)
#define STATS_NSS_QTY           (8)
#define STATS_HT_RECORDS  \
    (CLIENT_MAX_RADIO_WIDTH_QTY * \
     STATS_MSC_QTY * \
     STATS_NSS_QTY)

#define STATS_RECORDS  \
    (STATS_LEGACY_RECORDS + \
     STATS_HT_RECORDS)

typedef void radio_state_cb_t(
                struct schema_Wifi_Radio_State *rstate,
                schema_filter_t *filter);

typedef void radio_config_cb_t(
                struct schema_Wifi_Radio_Config *rconf,
                schema_filter_t *filter);

typedef struct
{
    // Client general data
    dpp_client_info_t               info;
    // Target specific client data
    wifi_associated_dev_stats_t     stats;
    wifi_associated_dev3_t          dev3;
    uint64_t                        stats_cookie;
    ds_dlist_node_t                 node;
} stats_client_record_t;

typedef stats_client_record_t target_client_record_t;

// on-channel survey
typedef struct
{
    uint64_t            chan_active;
    uint64_t            chan_busy;
    uint64_t            chan_busy_ext;
    uint64_t            chan_self;
    uint64_t            chan_rx;
    uint64_t            chan_tx;
    int32_t             chan_noise;
} stats_survey_bss_t;

// off-channel survey
typedef struct
{
    uint32_t            chan_active;
    uint32_t            chan_busy;
    uint32_t            chan_busy_ext;
    uint32_t            chan_self;
    uint32_t            chan_rx;
    uint32_t            chan_tx;
    int32_t             chan_noise;
} stats_survey_obss_t;

typedef struct
{
    // General survey data (upper layer cache key)
    dpp_survey_info_t   info;

    // Target specific survey data
    union {
        stats_survey_bss_t    survey_bss;
        stats_survey_obss_t   survey_obss;
    } stats;

    // Linked list of survey data
    ds_dlist_node_t     node;
} stats_survey_record_t;

typedef stats_survey_record_t    target_survey_record_t;

typedef struct
{
    uint64_t            chan_active;
    uint64_t            chan_tx;
    uint64_t            bytes_tx;
    uint64_t            samples;                     // obsolete
    uint64_t            queue[RADIO_QUEUE_MAX_QTY];  // obsolete
} stats_capacity_data_t;

typedef stats_capacity_data_t target_capacity_data_t;

typedef enum
{
    MACLEARN_TYPE_ETH   = 0,
    MACLEARN_TYPE_MOCA,
} maclearn_type_t;

typedef void (*sync_on_connect_cb_t)(void);

/* Current design requires caching key_id to have matching Wifi_VIF_Config/State tables.
 * To be removed in the future. */
typedef char psk_key_id_t[65];
extern psk_key_id_t *cached_key_ids;

bool                 maclearn_update(maclearn_type_t type,
                                    const char *mac,
                                    bool connected);

bool                 radio_cloud_mode_set(radio_cloud_mode_t mode);
radio_cloud_mode_t   radio_cloud_mode_get(void);
bool                 radio_rops_vstate(struct schema_Wifi_VIF_State *vstate,
                                       const char *radio_ifname);
void                 radio_trigger_resync(void);
INT                  get_radio_cap_index(const wifi_hal_capability_t *cap, INT radioIndex);
bool                 radio_ifname_to_idx(const char *ifname, INT *outRadioIndex);
bool                 radio_rops_vconfig(struct schema_Wifi_VIF_Config *vconf,
                                        const char *radio_ifname);

void                 sync_init(sync_mgr_t mgr,
                               sync_on_connect_cb_t sync_cb);
bool                 sync_cleanup(void);
bool                 sync_send_ssid_change(INT ssid_index, const char *ssid_ifname,
                                    const char *new_ssid);
#if !defined(CONFIG_RDK_DISABLE_SYNC) && !defined(CONFIG_RDK_MULTI_PSK_SUPPORT)
bool                 sync_send_security_change(INT ssid_index, const char *ssid_ifname,
                                    MeshWifiAPSecurity *sec);
#endif

bool                 sync_send_status(radio_cloud_mode_t mode);
bool                 sync_send_channel_change(INT radio_index, UINT channel);
bool                 sync_send_ssid_broadcast_change(INT ssid_index, BOOL ssid_broadcast);
bool                 sync_send_channel_bw_change(INT ssid_index, UINT bandwidth);

bool                 vif_state_update(INT ssidIndex);
bool                 vif_state_get(INT ssidIndex, struct schema_Wifi_VIF_State *vstate);
bool                 vif_copy_to_config(INT ssidIndex, struct schema_Wifi_VIF_State *vstate,
                                        struct schema_Wifi_VIF_Config *vconf);
bool                 vif_external_ssid_update(const char *ssid, int ssid_index);
bool                 vif_external_security_update(int ssid_index);
bool                 vif_external_acl_update(INT ssid_index);
bool                 vif_get_radio_ifname(INT ssidIndex, char *radio_ifname,
                                        size_t radio_ifname_size);
bool                 vif_ifname_to_idx(const char *ifname, INT *outSsidIndex);

struct               target_radio_ops;
bool                 clients_hal_init(const struct target_radio_ops *rops);
bool                 clients_hal_fetch_existing(unsigned int apIndex);

void                 sta_hal_init();

void                 cloud_config_mode_init(void);
void                 cloud_config_set_mode(const char *device_mode);

void                 dhcp_server_status_dispatch(void);
bool                 dhcp_server_resync_all_leases(void);

void                 wps_hal_init();
void                 wps_to_state(INT ssid_index, struct schema_Wifi_VIF_State *vstate);
void                 vif_config_set_wps(INT ssid_index,
                                        const struct schema_Wifi_VIF_Config *vconf,
                                        const struct schema_Wifi_VIF_Config_flags *changed,
                                        const char *radio_ifname);

void                multi_ap_hal_init();
void                vif_config_set_multi_ap(INT ssid_index,
                                        const char *multi_ap,
                                        const struct schema_Wifi_VIF_Config_flags *changed);
void                multi_ap_to_state(INT ssid_index,
                                        struct schema_Wifi_VIF_State *vstate);
bool                vap_controlled(const char *ifname);
bool                is_home_ap(const char *ifname);

bool                ssid_index_to_vap_info(UINT ssid_index, wifi_vap_info_map_t *map, wifi_vap_info_t **vap_info);

extern struct ev_loop   *wifihal_evloop;

#endif /* TARGET_INTERNAL_H_INCLUDED */
