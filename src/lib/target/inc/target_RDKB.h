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

#ifndef TARGET_RDKB_H_INCLUDED
#define TARGET_RDKB_H_INCLUDED

#include "schema.h"
#include "wifihal.h"

#define TARGET_LOG_PREFIX           "[Mesh]"
#define TARGET_LOG_HAS_LEVEL
#define TARGET_LOG_HAS_HOSTNAME

#define TARGET_RUN_PATH             "/var/run/plume"
#define TARGET_BIN_PATH             "/usr/plume/bin"
#define TARGET_TOOLS_PATH           "/usr/plume/tools"
#define TARGET_SCRIPTS_PATH         "/usr/plume/scripts"
#define TARGET_CERT_PATH            "/usr/plume/etc/certs"
#define TARGET_MANAGERS_PID_PATH    TARGET_RUN_PATH
#define TARGET_OVSDB_SOCK_PATH      "/var/run/openvswitch/db.sock"

#ifndef TARGET_LOGREAD_FILENAME
#define TARGET_LOGREAD_FILENAME     "/var/log/messages"
#endif

#define TARGET_PERSISTENT_STORAGE   "/nvram"

/******************************************************************************
 *  MANAGERS definitions
 *****************************************************************************/
#define TARGET_MANAGER_PATH(X)      "/usr/plume/bin/"X

/******************************************************************************
 *  TARGET INIT options  - Managers specific to RDK platform
 *****************************************************************************/
// This value should not collude with any existing manager from
// target_init_opt_t defined in target.h. Assigning a large value to avoid
// collusion
#define TARGET_INIT_MGR_RM          200

/******************************************************************************
 *  CLIENT definitions
 *****************************************************************************/
typedef wifihal_client_record_t target_client_record_t;

/******************************************************************************
 *  RADIO definitions
 *****************************************************************************/
extern char *   target_radio_get_chipset(const char *ifname);
extern bool     target_radio_config_get(char *ifname,
                                       struct schema_Wifi_Radio_Config *rconf);
extern bool     target_radio_state_get(char *ifname,
                                       struct schema_Wifi_Radio_State *rstate);

/******************************************************************************
 *  VIF definitions
 *****************************************************************************/
bool target_vif_config_get(char *ifname, struct schema_Wifi_VIF_Config *vconf);

/******************************************************************************
 *  SURVEY definitions
 *****************************************************************************/
typedef wifihal_survey_record_t    target_survey_record_t;

/******************************************************************************
 *  NEIGHBOR definitions
 *****************************************************************************/

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/
typedef wifihal_capacity_data_t target_capacity_data_t;

/******************************************************************************
 *  MAP definitions
 *****************************************************************************/
extern bool     target_map_update_vlan(const char *ifname, uint16_t vlan_id);
extern char *   target_map_ifname_to_bridge(const char *ifname);
extern char *   target_map_ifname_to_gre_bridge(const char *ifname);
extern char *   target_map_vlan_to_bridge(uint16_t vlan_id);
extern uint8_t  target_map_ifname_to_vif_radio_idx(const char *ifname);
extern uint16_t target_map_ifname_to_vlan(const char *ifname);
extern uint16_t target_map_bridge_to_vlan(const char *bridge);

/******************************************************************************
 *  STATS counters type: cumulative (1) or deltas (0)
 *****************************************************************************/
// if not defined, default to cumulative counters
#ifndef STATS_CUMULATIVE_SURVEY_ONCHAN
#define STATS_CUMULATIVE_SURVEY_ONCHAN  1
#endif

#ifndef STATS_CUMULATIVE_SURVEY_OFFCHAN
#define STATS_CUMULATIVE_SURVEY_OFFCHAN 1
#endif

/******************************************************************************
 *  DHCP definitions
 *****************************************************************************/
extern bool     target_dhcp_lease_parse_file(char *filename);
extern bool     target_dhcp_lease_upsert(struct schema_DHCP_leased_IP *dlip);
extern bool     target_dhcp_lease_remove(struct schema_DHCP_leased_IP *dlip);

#include "target_common.h"

#ifndef RADIO_MAX_DEVICE_QTY
#define RADIO_MAX_DEVICE_QTY 2
#endif

#endif  /* TARGET_RDKB_H_INCLUDED */
