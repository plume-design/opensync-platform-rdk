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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>


#include "connector.h"
#include "log.h"
#include "kconfig.h"

#include "connector_main.h"
#include "connector_lan.h"


/******************************************************************************/

/******************************************************************************/
struct connector_ovsdb_api* connector_api;

ANSC_HANDLE ccsp_bus_handle = NULL;

bool connector_init(struct ev_loop *loop, const struct connector_ovsdb_api *api)
{
    DmErr_t    err = 0;

    connector_api = (struct connector_ovsdb_api*)api;

    LOGI("Initializing Ccsp bus");
    err = CCSP_Message_Bus_Init(
            "com.cisco.spvtg.ccsp.opensync",
            CCSP_MSG_BUS_CFG,
            &ccsp_bus_handle,
            (CCSP_MESSAGE_BUS_MALLOC)Ansc_AllocateMemory_Callback,
            Ansc_FreeMemory_Callback);

    if (err != 0)
    {
        LOGE("CCSP_Message_Bus_Init: bus initialization failed: %s %d", Cdm_StrError(err), err);
        return false;
    }

    err = Cdm_Init(ccsp_bus_handle, NULL, NULL, NULL, "com.cisco.spvtg.ccsp.opensync");
    if (err != CCSP_SUCCESS)
    {
        LOGE("Cdm_Init failed: %s", Cdm_StrError(err));
        return false;
    }

    LOGI("Populating OVSDB from RDK");

    if (connector_lan_br_config_push_ovsdb(connector_api))
        LOGW("Failed to push lan config to OVSDB");

    return true;
}

bool connector_close(struct ev_loop *loop)
{
    /* Close access to your DB */
    return true;
}

bool connector_sync_mode(const connector_device_mode_e mode)
{
    /* Handle device mode */
    return true;
}

bool connector_sync_radio(const struct schema_Wifi_Radio_Config *rconf)
{
    /*
     * You can go over all radio settings or process just _changed flags to populate your DB
     *  Example: if (rconf.channel_changed) set_new_channel(rconf.channel);
     */

    return true;
}

bool connector_sync_vif(const struct schema_Wifi_VIF_Config *vconf)
{
    /*
     * You can go over all radio settings or process just _changed flags to populate your DB
     *  Example: if (vconf.ssid_changed) set_new_ssid(vconf.ssid);
     */

    return true;
}

bool connector_sync_inet(const struct schema_Wifi_Inet_Config *iconf)
{
    if (!strcmp(iconf->if_name, CONFIG_TARGET_LAN_BRIDGE_NAME))
    {
        if (iconf->inet_addr_changed || iconf->netmask_changed || iconf->dhcpd_changed )
        {
            connector_lan_br_config_push_rdk(iconf);
        }
    }

    return true;
}
