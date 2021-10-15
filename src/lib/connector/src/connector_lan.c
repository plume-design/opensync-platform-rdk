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
#include "os_types.h"
#include "os_util.h"
#include "os_nif.h"
#include "util.h"
#include "os.h"
#include "log.h"
#include "const.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "connector_main.h"
#include "connector_lan.h"

extern  ANSC_HANDLE                        ccsp_bus_handle;

bool get_dhcp_configuration(struct schema_Wifi_Inet_Config *tmp)
{
    parameterValStruct_t **valStructs = NULL;
    const char dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    char *paramNames[] = {"Device.DHCPv4.Server.Pool.1.MinAddress",
            "Device.DHCPv4.Server.Pool.1.MaxAddress",
            "Device.DHCPv4.Server.Pool.1.LeaseTime",
            "Device.DHCPv4.Server.Pool.1.IPRouters"};
    int valNum = 0;
    int ret = 0;
    int lease_time = 0;

    LOGD("Updating DHCP configuration to OVSDB");

    ret = CcspBaseIf_getParameterValues(
            ccsp_bus_handle,
            dstComponent,
            dstPath,
            paramNames,
            (int)ARRAY_SIZE(paramNames),
            &valNum,
            &valStructs);

    if (CCSP_Message_Bus_OK != ret)
    {
         LOGE("CcspBaseIf_getParameterValues: Failed to get DHCP configuration error %d\n", ret);
         free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
         return false;
    }

    SCHEMA_KEY_VAL_APPEND(tmp->dhcpd, "start", valStructs[0]->parameterValue);
    SCHEMA_KEY_VAL_APPEND(tmp->dhcpd, "stop", valStructs[1]->parameterValue);
    lease_time = strtol(valStructs[2]->parameterValue, NULL, 10);
    SCHEMA_KEY_VAL_APPEND(tmp->dhcpd, "lease_time", strfmta("%dh", lease_time / 3600));
    SCHEMA_KEY_VAL_APPEND(tmp->dhcpd, "dhcp_option", strfmta("3,%s;6,%s", valStructs[3]->parameterValue, valStructs[3]->parameterValue));
    SCHEMA_KEY_VAL_APPEND(tmp->dhcpd, "force", "false");

    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
    return true;
}

bool set_dhcp_configuration(const struct schema_Wifi_Inet_Config *inet)
{
    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)ccsp_bus_handle;
    parameterValStruct_t param_val[2];
    const char dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    char *faultParam = NULL;
    char *paramNames[] = {"Device.DHCPv4.Server.Pool.1.MinAddress",
            "Device.DHCPv4.Server.Pool.1.MaxAddress"};
    int ret = 0;
    int i = 0;

    LOGD("Updating DHCP configuration to RDK");

    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        param_val[i].parameterName = paramNames[i];
        param_val[i].type = ccsp_string;
    }
    param_val[0].parameterValue = (char *)SCHEMA_KEY_VAL(inet->dhcpd, "start");
    param_val[1].parameterValue = (char *)SCHEMA_KEY_VAL(inet->dhcpd, "stop");

    ret = CcspBaseIf_setParameterValues(
              ccsp_bus_handle,
              dstComponent,
              dstPath,
              0,
              0,
              (parameterValStruct_t *)&param_val,
              (int)ARRAY_SIZE(paramNames),
              true,
              &faultParam);

    if (ret != CCSP_SUCCESS)
    {
        LOGE("CcspBaseIf_setParameterValues: Failed to set DHCP (%s)", faultParam ? faultParam : "Uknown Fault");
        if (faultParam)
            bus_info->freefunc(faultParam);

        return false;
    }
    return true;
}

int connector_lan_br_config_push_ovsdb(struct connector_ovsdb_api *connector_api)
{
    struct schema_Wifi_Inet_Config tmp;

    LOGI("Setting LAN interface/dhcp to ovsdb");

    memset(&tmp, 0, sizeof(tmp));
    SCHEMA_SET_STR(tmp.if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
    tmp._partial_update = true;

    if (!get_dhcp_configuration(&tmp))
        return -1;

    if (connector_api->connector_inet_update_cb(&tmp) == false)
        return -1;

    return 0;
}

int connector_lan_br_config_push_rdk(const struct schema_Wifi_Inet_Config *inet)
{

    LOGI("Setting LAN interface/dhcp to RDK");

    if (inet->dhcpd_changed && !set_dhcp_configuration(inet))
        return -1;

    //TODO: implement other options

    return 0;

}
