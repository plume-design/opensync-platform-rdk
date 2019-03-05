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


#define MODULE_ID LOG_MODULE_ID_VIF


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

bool
target_vif_config_set(char *ifname, struct schema_Wifi_VIF_Config *vconf)
{
    return wifihal_vif_update(target_map_ifname(ifname), vconf);
}

bool
target_vif_config_get(char *ifname, struct schema_Wifi_VIF_Config *vconf)
{
    return wifihal_vif_config_get(target_map_ifname(ifname), vconf);
}

bool
target_vif_config_register(char *ifname, target_vif_config_cb_t *vconf_cb)
{
    return wifihal_vif_config_register(ifname, vconf_cb);
}

bool
target_vif_state_get(char *ifname, struct schema_Wifi_VIF_State *vstate)
{
    schema_filter_t filter;
    return wifihal_vif_state_get(target_map_ifname(ifname), vstate, &filter);
}

bool
target_vif_state_register(char *ifname, target_vif_state_cb_t *vstate_cb)
{
    return wifihal_vif_state_register(ifname, vstate_cb);
}
