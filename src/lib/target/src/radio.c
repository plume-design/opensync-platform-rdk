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
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#include "log.h"
#include "const.h"
#include "target.h"
#include "wifihal.h"


#define MODULE_ID LOG_MODULE_ID_RADIO


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

bool
target_radio_config_init(ds_dlist_t *init_cfg)
{
    target_radio_cfg_t      *tradio;
    target_vif_cfg_t        *tvif;
    wifihal_radio_t         *radio;
    wifihal_ssid_t          *ssid;
    ds_dlist_t              *radios;

    radios = wifihal_get_radios();
    if (!radios || ds_dlist_is_empty(radios))
    {
        return false;
    }

    ds_dlist_init(init_cfg, target_radio_cfg_t, dsl_node);
    ds_dlist_foreach(radios, radio)
    {
        if (!(tradio = calloc(1, sizeof(*tradio))))
        {
            LOGEM("%s: Failed to allocate memory for new target radio",
                  radio->ifname);
            return false;
        }

        if (!target_radio_config_get(target_unmap_ifname(radio->ifname),
                                     &tradio->rconf))
        {
            LOGW("%s: Skipping - Failed to retrieve configuration",
                 radio->ifname);
            free(tradio);
            continue;
        }

        ds_dlist_init(&tradio->vifs_cfg, target_vif_cfg_t, dsl_node);
        ds_dlist_foreach(&radio->ssids, ssid)
        {
            if (!(tvif = calloc(1, sizeof(*tvif))))
            {
                LOGEM("%s: Failed to allocate memory for VIF %s",
                      radio->ifname, ssid->ifname);
                return false;
            }

            if (!target_vif_config_get(target_unmap_ifname(ssid->ifname),
                                       &tvif->vconf))
            {
                LOGW("%s: Skipping VIF %s - Failed to retrieve config",
                     radio->ifname, ssid->ifname);
                free(tvif);
                continue;
            }

            ds_dlist_insert_tail(&tradio->vifs_cfg, tvif);
        }

        ds_dlist_insert_tail(init_cfg, tradio);
    }

    if (ds_dlist_is_empty(init_cfg))
    {
        return false;
    }

    return true;
}

bool
target_radio_config_set(char *ifname, struct schema_Wifi_Radio_Config *rconf)
{
    return wifihal_radio_update(ifname, rconf);
}

bool
target_radio_config_get(char *ifname, struct schema_Wifi_Radio_Config *rconf)
{
    return wifihal_radio_config_get(target_map_ifname(ifname), rconf);
}

bool
target_radio_config_register(char *ifname, target_radio_config_cb_t *rconf_cb)
{
    return wifihal_radio_config_register(ifname, rconf_cb);
}

bool
target_radio_state_get(char *ifname, struct schema_Wifi_Radio_State *rstate)
{
    schema_filter_t filter;
    return wifihal_radio_state_get(target_map_ifname(ifname), rstate, &filter);
}

bool
target_radio_state_register(char *ifname, target_radio_state_cb_t *radio_state_cb)
{
    return wifihal_radio_state_register(ifname, radio_state_cb);
}
