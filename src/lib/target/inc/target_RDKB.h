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
#include "target_internal.h"

#define TARGET_CERT_PATH            "/usr/opensync/etc/certs"
#define TARGET_OVSDB_SOCK_PATH      "/var/run/openvswitch/db.sock"

#ifndef TARGET_LOGREAD_FILENAME
#define TARGET_LOGREAD_FILENAME     "/var/log/messages"
#endif

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

/******************************************************************************
 *  NEIGHBOR definitions
 *****************************************************************************/

/******************************************************************************
 *  DEVICE definitions
 *****************************************************************************/

/******************************************************************************
 *  CAPACITY definitions
 *****************************************************************************/

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

#include "target_common.h"

#ifndef RADIO_MAX_DEVICE_QTY
#define RADIO_MAX_DEVICE_QTY 3
#endif

#endif  /* TARGET_RDKB_H_INCLUDED */
