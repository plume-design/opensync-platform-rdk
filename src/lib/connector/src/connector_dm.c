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
#include "connector_dm.h"

#define BUFF_LEN 51
#define NUM_OF_ENTRIES 7
extern  ANSC_HANDLE                        ccsp_bus_handle;

#ifdef CONFIG_RDK_CONNECTOR_DHCP_SYNC_LAN_MANAGEMENT
bool set_lan_management(const struct schema_Wifi_Inet_Config *inet)
{
    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)ccsp_bus_handle;
    parameterValStruct_t param_val[2];
    const char dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    char *faultParam = NULL;
    char *paramNames[] = {"Device.X_CISCO_COM_DeviceControl.LanManagementEntry.1.LanIPAddress",
            "Device.X_CISCO_COM_DeviceControl.LanManagementEntry.1.LanSubnetMask"};
    int ret = 0;
    int i = 0;

    LOGI("Updating LAN Management configuration to RDK");

    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        param_val[i].parameterName = paramNames[i];
        param_val[i].type = ccsp_string;
    }
    param_val[0].parameterValue = (char *)inet->inet_addr;
    param_val[1].parameterValue = (char *)inet->netmask;

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
        LOGE("CcspBaseIf_setParameterValues: Failed to set LAN Management (%s)", faultParam ? faultParam : "Unknown Fault");
        if (faultParam)
            bus_info->freefunc(faultParam);

        return false;
    }
    LOGI("Updating LAN Management configuration to RDK is completed");
    return true;
}
#endif

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

    LOGI("Updating DHCP configuration to OVSDB");

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
         LOGE("CcspBaseIf_getParameterValues: Failed to get DHCP configuration error %d", ret);
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

    LOGI("Updating DHCP configuration to RDK");

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
        LOGE("CcspBaseIf_setParameterValues: Failed to set DHCP (%s)", faultParam ? faultParam : "Unknown Fault");
        if (faultParam)
            bus_info->freefunc(faultParam);

        return false;
    }
    LOGI("Updating DHCP configuration to RDK is completed");
    return true;
}

int connector_lan_br_config_push_ovsdb_dm(struct connector_ovsdb_api *connector_api)
{
    struct schema_Wifi_Inet_Config tmp;

    LOGI("Setting LAN interface/DHCP to ovsdb");

    memset(&tmp, 0, sizeof(tmp));
    SCHEMA_SET_STR(tmp.if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
    tmp._partial_update = true;

    if (!get_dhcp_configuration(&tmp))
        return -1;

    if (connector_api->connector_inet_update_cb(&tmp) == false)
    {
        LOGE("connector_inet_update_cb failed");
        return -1;
    }

    LOGI("Setting LAN interface/DHCP to ovsdb is completed");
    return 0;
}

int connector_lan_br_config_push_rdk_dm(const struct schema_Wifi_Inet_Config *inet)
{
    LOGI("Setting LAN interface/DHCP to RDK");

#ifdef CONFIG_RDK_CONNECTOR_DHCP_SYNC_LAN_MANAGEMENT
    if (inet->inet_addr_changed && !set_lan_management(inet))
        return -1;
#endif

    if (inet->dhcpd_changed && !set_dhcp_configuration(inet))
        return -1;

    return 0;
}

/* IP_Port_Forward -> Device.NAT.PortMapping. sync */

bool set_portforward_configuration(const struct schema_IP_Port_Forward *inet)
{
    CCSP_MESSAGE_BUS_INFO       *bus_info = (CCSP_MESSAGE_BUS_INFO *)ccsp_bus_handle;
    parameterValStruct_t        param_val[NUM_OF_ENTRIES];
    const char                  dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                        dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    char                        *faultParam = NULL;
    char                        internalClient[BUFF_LEN], externalPort[BUFF_LEN], externalPortEndRange[BUFF_LEN];
    char                        protocol_in[BUFF_LEN], internalPort[BUFF_LEN], description_in[BUFF_LEN], enable[BUFF_LEN];
    char                        *paramNames[] = {internalClient, externalPort, externalPortEndRange, protocol_in, internalPort, description_in, enable};
    char                        *path_str[] = {"Device.NAT.PortMapping."};
    int                         i = 0, new_entry_idx = 0, ret = 0;
    char                        src_port[20], dst_port[20], protocol[4] = "", description[BUFF_LEN] = "";
    const char                  true_str[] = "TRUE";
    const char                  *name_formats[] = {"Device.NAT.PortMapping.%d.InternalClient",
                                    "Device.NAT.PortMapping.%d.ExternalPort",
                                    "Device.NAT.PortMapping.%d.ExternalPortEndRange",
                                    "Device.NAT.PortMapping.%d.Protocol",
                                    "Device.NAT.PortMapping.%d.InternalPort",
                                    "Device.NAT.PortMapping.%d.Description",
                                    "Device.NAT.PortMapping.%d.Enable"};

    /* Need to set Portmapping table for that we need to give required paths to CcspBaseIf_setParameterValues */
    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        memset(paramNames[i], 0, BUFF_LEN);
    }

    memset(src_port, 0, sizeof(src_port));
    memset(dst_port, 0, sizeof(dst_port));

    ret = CcspBaseIf_AddTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str[0], &new_entry_idx);
    if (CCSP_Message_Bus_OK != ret)
    {
        LOGE("CcspBaseIf_AddTblRow: Failed to add port forwarding configuration error %d", ret);
        return false;
    }

    /* list of portmapping parameters, need to  update */
    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        sprintf(paramNames[i], name_formats[i], new_entry_idx);
    }

    /* updating  name_formats to parameterValStruct_t */
    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        param_val[i].parameterName = paramNames[i];
    }

    /* Filling parametervalues with inet (des_ipaddr,src_port,dst_port,protocol,description) */
    param_val[0].parameterValue = (char *)inet->dst_ipaddr;
    param_val[0].type = ccsp_string;

    sprintf(src_port, "%d", inet->src_port);
    param_val[1].parameterValue = (char *)src_port;
    param_val[1].type = ccsp_unsignedInt;

    param_val[2].parameterValue = (char *)src_port;
    param_val[2].type = ccsp_unsignedInt;

    sprintf(protocol, "%s", strcmp(inet->protocol, "tcp") == 0 ? "TCP" : "UDP");
    param_val[3].parameterValue = (char *)protocol;
    param_val[3].type = ccsp_string;

    sprintf(dst_port, "%d", inet->dst_port);
    param_val[4].parameterValue = (char *)dst_port;
    param_val[4].type = ccsp_unsignedInt;

    sprintf(description, "OpenSync Rule %d", new_entry_idx);
    param_val[5].parameterValue = (char *)description;
    param_val[5].type = ccsp_string;

    param_val[6].parameterValue = (char *)true_str;
    param_val[6].type = ccsp_boolean;

    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        LOGD("param_val[%d].parameterName-->[%s] param_val[%d].parameterValue-->[%s] param_val[%d].type-->[%d]",
              i, param_val[i].parameterName, i, param_val[i].parameterValue, i, param_val[i].type);
    }

    LOGD("Number of entries present in RDK DB:[%d], array size is [%d]", new_entry_idx, (int)ARRAY_SIZE(paramNames));

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
        LOGE("CcspBaseIf_setParameterValues: Failed to set Port Forward (%s) (%p)", faultParam ? faultParam : "Unknown Fault", bus_info);
         if (faultParam)
            bus_info->freefunc(faultParam);

        return false;
    }

    LOGI("Port forward setting to RDK is completed");
    return true;
}

void portforward_push_dm(const struct schema_IP_Port_Forward *inet)
{
    LOGI("Setting port forward to RDK");
    set_portforward_configuration(inet);

    return;
}

int portforward_del_dm(const struct schema_IP_Port_Forward *iconf)
{
    char                     path_str[BUFF_LEN];
    parameterValStruct_t     **valStructs = NULL;
    const char               dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                     dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    int                      valNum = 0;
    int                      ret = 0, i = 0, idx = 0, k = 0, src_port_index = 0, dest_port_index = 0, protocol_index = 0;
    char                     *token;
    char                     *paramNames[] = {"Device.NAT.PortMapping."};

    memset(path_str, 0, sizeof(path_str));
    LOGI("portforward_del_dm: Attempt of removing port forwarding element.");

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
         LOGE("CcspBaseIf_getParameterValues: Failed to get Portforward configuration error %d", ret);
         free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
         return false;
    }

    for (i = 0; i < valNum; i++)
    {
        token = strtok(valStructs[i]->parameterName + strlen(paramNames[0]), ".");
        idx = atoi(token);

        token = strtok(NULL, ".");
        if (strcmp("Enable", token) == 0) k++;
        if (0 == strcmp("ExternalPort", token)) src_port_index = i;
        if (0 == strcmp("InternalPort", token)) dest_port_index = i;
        if (0 == strcmp("Protocol" , token)) protocol_index = i;
        if (0 == strcmp("InternalClient", token))
        {
            LOGD("Found matching Portforward ID: %d Clint_ip: %s src_port: %d dest_port: %d",
                  idx, valStructs[i]->parameterValue, atoi(valStructs[src_port_index]->parameterValue), atoi(valStructs[dest_port_index]->parameterValue));

            if (    (0 == strcmp(iconf->dst_ipaddr, valStructs[i]->parameterValue))
                &&  (0 == strcasecmp(iconf->protocol, valStructs[protocol_index]->parameterValue))
                &&  (iconf->src_port == atoi(valStructs[src_port_index]->parameterValue))
                &&  (iconf->dst_port == atoi(valStructs[dest_port_index]->parameterValue))
               )
            {
                sprintf(path_str, "Device.NAT.PortMapping.%d.", idx);
                ret = CcspBaseIf_DeleteTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str);

                LOGI("Removing port forwarding elements: ipaddr: %s protocol: %s src_port: %d dst_port: %d return_status: %d",
                      iconf->dst_ipaddr, iconf->protocol, iconf->src_port, iconf->dst_port, ret);

                if (CCSP_Message_Bus_OK != ret)
                {
                    LOGE("CcspBaseIf_DeleteTblRow: Failed to delete Portforward configuration error %d", ret);
                    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
                    return false;
                }
                break;
            }
        }
    }
    LOGI("portforward_del_dm: Removed port forwarding element from the entry");
    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
    return true;
}

int portforward_clean_dm(void)
{
    char                     path_str[BUFF_LEN];
    parameterValStruct_t     **valStructs = NULL;
    const char               dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                     dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    int                      ret = 0, i = 0, idx = 0, valNum = 0;
    char                     *token;
    char                     *paramNames[] = {"Device.NAT.PortMapping."};

    memset(path_str, 0, sizeof(path_str));
    LOGI("portforward_clean_dm: Removing port forward entries from the table");

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
         LOGE("CcspBaseIf_getParameterValues: Failed to get Portforward mapping configuration error %d", ret);
         free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
         return 1;
    }

    for (i = 0; i < valNum; i++)
    {
        token = strtok(valStructs[i]->parameterName + strlen(paramNames[0]), ".");
        idx = atoi(token);

        token = strtok(NULL, ".");
        if (strcmp("Enable", token) == 0)
        {
            sprintf(path_str, "Device.NAT.PortMapping.%d.", idx);
            ret = CcspBaseIf_DeleteTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str);
            if (CCSP_Message_Bus_OK != ret)
            {
                LOGE("CcspBaseIf_DeleteTblRow: Failed to delete Portforward mapping configuration error %d", ret);
            }
        }
    }

    LOGI("portforward_clean_dm: Removed port forward entries from the table");
    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
    return 0;
}

/*
 * DHCP_reserved_IP -> Device.DHCPv4.Server.Pool.1.StaticAddress. sync
 */

bool set_dhcp_reservation_config(const struct schema_DHCP_reserved_IP *inet)
{
    CCSP_MESSAGE_BUS_INFO            *bus_info = (CCSP_MESSAGE_BUS_INFO *)ccsp_bus_handle;
    parameterValStruct_t             param_val[2];
    const char                       dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                             dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    char                             *faultParam = NULL;
    char                             chaddr[BUFF_LEN], yiaddr[BUFF_LEN];
    char                             *paramNames[] = {chaddr, yiaddr};
    int                              ret = 0;
    int                              i = 0, reserveip = 0;
    char                             *path_str[] = {"Device.DHCPv4.Server.Pool.1.StaticAddress."};

    LOGI("Updating DHCP reservation configuration to RDK");

    memset(chaddr, 0, sizeof(chaddr));
    memset(yiaddr, 0, sizeof(yiaddr));

    ret = CcspBaseIf_AddTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str[0], &reserveip);
    if (CCSP_Message_Bus_OK != ret)
    {
        LOGE("CcspBaseIf_AddTblRow: Failed to add DHCP configuration error %d", ret);
        return false;
    }
    sprintf(chaddr, "Device.DHCPv4.Server.Pool.1.StaticAddress.%d.Chaddr", reserveip);
    sprintf(yiaddr, "Device.DHCPv4.Server.Pool.1.StaticAddress.%d.Yiaddr", reserveip);
    for (i = 0; i < (int)ARRAY_SIZE(paramNames); i++)
    {
        param_val[i].parameterName = paramNames[i];
        param_val[i].type = ccsp_string;
    }
    param_val[0].parameterValue = (char *)inet->hw_addr;
    param_val[1].parameterValue = (char *)inet->ip_addr;

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
        LOGE("CcspBaseIf_setParameterValues: Failed to set DHCP (%s)", faultParam ? faultParam : "Unknown Fault");
        if (faultParam)
            bus_info->freefunc(faultParam);

        return false;
    }

    LOGI("Updated DHCP reservation configuration ip_addr:%s, hw_addr:%s", inet->ip_addr, inet->hw_addr);
    return true;
}

int dhcp_reservation_push_dm(const struct schema_DHCP_reserved_IP *inet)
{
    LOGI("Setting DHCP reservation to RDK");
    if (!set_dhcp_reservation_config(inet))
        return -1;

    return 0;
}

int dhcp_reservation_del_dm(const struct schema_DHCP_reserved_IP *rip)
{
    char                     path_str[BUFF_LEN];
    parameterValStruct_t     **valStructs = NULL;
    const char               dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                     dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    int                      ret = 0, i = 0, k = 0, id_chaddr = 0, idx = 0, valNum = 0;
    char                     *token;
    char                     *paramNames[] = {"Device.DHCPv4.Server.Pool.1.StaticAddress."};

    memset(path_str, 0, sizeof(path_str));
    LOGI("dhcp_reservation_del_dm: Deleting DHCP reservation entry from the table");

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
         LOGE("CcspBaseIf_getParameterValues: Failed to get DHCP configuration error %d", ret);
         free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
         return false;
    }

    for (i = 0; i < valNum; i++)
    {
        token = strtok(valStructs[i]->parameterName + strlen(paramNames[0]), ".");
        idx = atoi(token);

        token = strtok(NULL, ".");
        if (strcmp("Enable", token) == 0) k++;
        if (strcmp("Chaddr", token) == 0) id_chaddr = i;
        if (strcmp("Yiaddr", token) == 0)
        {
            if ((strcmp(rip->ip_addr, valStructs[i]->parameterValue) == 0) && (strcmp(rip->hw_addr, valStructs[id_chaddr]->parameterValue) == 0))
            {
                sprintf(path_str, "Device.DHCPv4.Server.Pool.1.StaticAddress.%d.", idx);
                ret = CcspBaseIf_DeleteTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str);

                LOGI("Found matching DHCP reservation ID: %d IP: %s MAC: %s. Removing entry, return_value:%d",
                      idx, valStructs[i]->parameterValue, valStructs[id_chaddr]->parameterValue, ret);

                if (CCSP_Message_Bus_OK != ret)
                {
                    LOGE("CcspBaseIf_DeleteTblRow: Failed to delete DHCP configuration error %d", ret);
                    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
                    return false;
                }
                break;
            }
        }
    }

    LOGI("dhcp_reservation_del_dm: Deleted DHCP reservation entry");
    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
    return true;
}

int dhcp_reservation_clean_dm(void)
{
    char                     path_str[BUFF_LEN];
    parameterValStruct_t     **valStructs = NULL;
    const char               dstComponent[] = "eRT.com.cisco.spvtg.ccsp.pam";
    char                     dstPath[] = "/com/cisco/spvtg/ccsp/pam";
    int                      ret = 0, i = 0, idx = 0, valNum = 0;
    char                     *token;
    char                     *paramNames[] = {"Device.DHCPv4.Server.Pool.1.StaticAddress."};

    memset(path_str, 0, sizeof(path_str));
    LOGI("dhcp_reservation_clean_dm: Removing all DHCP reservation entries");

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
         LOGE("CcspBaseIf_getParameterValues: Failed to get DHCP configuration error %d", ret);
         free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
         return false;
    }

    for (i = 0; i < valNum; i++)
    {
        token = strtok(valStructs[i]->parameterName + strlen(paramNames[0]), ".");
        idx = atoi(token);

        token = strtok(NULL, ".");
        if (strcmp("Enable", token) == 0)
        {
            sprintf(path_str, "Device.DHCPv4.Server.Pool.1.StaticAddress.%d.", idx);
            ret = CcspBaseIf_DeleteTblRow(ccsp_bus_handle, dstComponent, dstPath, 0, path_str);
            if (CCSP_Message_Bus_OK != ret)
            {
                LOGE("CcspBaseIf_DeleteTblRow: Failed to delete DHCP configuration error %d", ret);
            }
        }
    }

    LOGI("dhcp_reservation_clean_dm: Removed DHCP reservation entries");
    free_parameterValStruct_t(ccsp_bus_handle, valNum, valStructs);
    return 0;
}
