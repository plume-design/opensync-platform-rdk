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

#include "schema.h"
#include "dpp_types.h"
#include "dpp_client.h"
#include "dpp_survey.h"
#include "dpp_neighbor.h"
#include "osync_hal.h"
#include "osn_dhcp.h"

#include <mesh/meshsync_msgs.h>  // needed only by sync_send_security_change()

#ifndef __WIFI_HAL_H__
#include "ccsp/wifi_hal.h"
#endif

#define WIFIHAL_MAX_BUFFER          64
#define WIFIHAL_MAX_MACSTR          18

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
    wifi_associated_dev2_t          dev2;
    wifi_associated_dev_rate_info_rx_stats_t stats_rx[STATS_RECORDS];
    wifi_associated_dev_rate_info_tx_stats_t stats_tx[STATS_RECORDS];
    unsigned                        num_rx;
    unsigned                        num_tx;
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

bool                 maclearn_update(maclearn_type_t type,
                                    const char *mac,
                                    bool connected);

bool                 radio_cloud_mode_set(radio_cloud_mode_t mode);
radio_cloud_mode_t   radio_cloud_mode_get(void);
bool                 radio_rops_vstate(struct schema_Wifi_VIF_State *vstate);
void                 radio_trigger_resync(void);
bool                 radio_ifname_to_idx(const char *ifname, INT *outRadioIndex);
bool                 radio_rops_vconfig(struct schema_Wifi_VIF_Config *vconf,
                                        const char *radio_ifname);

void                 clients_connection(INT apIndex,
                                    char *mac, char *key_id);
void                 clients_disconnection(INT apIndex,
                                    char *mac);

void                 sync_init(sync_mgr_t mgr,
                               sync_on_connect_cb_t sync_cb);
bool                 sync_cleanup(void);
bool                 sync_send_ssid_change(INT ssid_index, const char *ssid_ifname,
                                    const char *new_ssid);
bool                 sync_send_security_change(INT ssid_index, const char *ssid_ifname,
                                    MeshWifiAPSecurity *sec);
bool                 sync_send_status(radio_cloud_mode_t mode);
bool                 sync_send_channel_change(INT radio_index, UINT channel);
bool                 sync_send_ssid_broadcast_change(INT ssid_index, BOOL ssid_broadcast);
bool                 sync_send_channel_bw_change(INT ssid_index, UINT bandwidth);

bool                 vif_state_update(INT ssidIndex);
bool                 vif_state_get(INT ssidIndex, struct schema_Wifi_VIF_State *vstate);
bool                 vif_copy_to_config(INT ssidIndex, struct schema_Wifi_VIF_State *vstate,
                                        struct schema_Wifi_VIF_Config *vconf);
bool                 vif_external_ssid_update(const char *ssid, int ssid_index);
bool                 vif_external_security_update(int ssid_index, const char *passphrase,
                                        const char *secMode);

struct               target_radio_ops;
bool                 clients_hal_init(const struct target_radio_ops *rops);
bool                 clients_hal_fetch_existing(unsigned int apIndex);

void                 cloud_config_mode_init(void);
void                 cloud_config_set_mode(const char *device_mode);

bool                 dhcp_lease_upsert(const osync_hal_dhcp_lease_t *dlip);
void                 dhcp_lease_clear(osn_dhcp_server_t *self);
void                 dhcp_server_status_dispatch(void);

extern struct ev_loop   *wifihal_evloop;

#endif /* TARGET_INTERNAL_H_INCLUDED */
