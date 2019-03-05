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
 * wifihal_acl.c
 *
 * RDKB Platform - Wifi HAL - ACL Mapping
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/ether.h>
#include <ev.h>

#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID   LOG_MODULE_ID_HAL

#define WIFIHAL_ACL_BUF_SIZE   1024

/*****************************************************************************/

#ifdef QCA_WIFI

#define IWPRIV_ADDMAC       "addmac"
#define IWPRIV_DELMAC       "delmac"
#define IWPRIV_GETMAC       "getmac"
#define IWPRIV_MACCMD       "maccmd"
#define IWPRIV_GET_MACCMD   "get_maccmd"

enum
{
    WEXT_ACL_MODE_DISABLE       = 0,
    WEXT_ACL_MODE_WHITELIST,
    WEXT_ACL_MODE_BLACKLIST,
    WEXT_ACL_MODE_FLUSH
};

static c_item_t map_acl_modes[] = {
    C_ITEM_STR(WEXT_ACL_MODE_DISABLE,   "none"),
    C_ITEM_STR(WEXT_ACL_MODE_WHITELIST, "whitelist"),
    C_ITEM_STR(WEXT_ACL_MODE_BLACKLIST, "blacklist"),
    C_ITEM_STR(WEXT_ACL_MODE_FLUSH,     "flush")
};

#else /* not QCA_WIFI */

enum
{
    HAL_ACL_MODE_DISABLE        = 0,
    HAL_ACL_MODE_WHITELIST,
    HAL_ACL_MODE_BLACKLIST
};

static c_item_t map_acl_modes[] = {
    C_ITEM_STR(HAL_ACL_MODE_DISABLE,    "none"),
    C_ITEM_STR(HAL_ACL_MODE_WHITELIST,  "whitelist"),
    C_ITEM_STR(HAL_ACL_MODE_BLACKLIST,  "blacklist")
};

#endif /* not QCA_WIFI */

/*****************************************************************************/

bool
wifihal_acl_to_config(wifihal_ssid_t *ssid, struct schema_Wifi_VIF_Config *vconf)
{
    char                    acl_buf[WIFIHAL_ACL_BUF_SIZE];
    char                    *p;
    char                    *s = NULL;
    INT                     acl_mode;
    INT                     status = RETURN_ERR;
    INT                     i;
#ifdef QCA_WIFI
    char                    *unmapped_ifname = target_unmap_ifname(ssid->ifname);

    // Band steering on QCA uses the ACL on home interfaces, so ignore it
    // for now and report none to the cloud
    if (!strncmp(unmapped_ifname, "home-ap-", 8))
    {
        strncpy(vconf->mac_list_type,
                c_get_str_by_key(map_acl_modes, WEXT_ACL_MODE_DISABLE),
                sizeof(vconf->mac_list_type)-1);
        vconf->mac_list_len = 0;
        return true;
    }
#endif /* QCA_WIFI */

    WIFIHAL_TM_START();
    status = wifi_getApMacAddressControlMode(ssid->index, &acl_mode);
    WIFIHAL_TM_STOP(status, WIFIHAL_STD_TIME, "wifi_getApMacAddressControlMode(%d, %d)",
                                             ssid->index, acl_mode);
    if (status != RETURN_OK) {
        LOGE("%s: Failed to get ACL mode", ssid->ifname);
        return false;
    }

    strncpy(vconf->mac_list_type,
            c_get_str_by_key(map_acl_modes, acl_mode),
            sizeof(vconf->mac_list_type)-1);
    if (strlen(vconf->mac_list_type) == 0) {
        LOGE("%s: Unknown ACL mode (%u)", ssid->ifname, acl_mode);
        return false;
    }
    vconf->mac_list_type_exists = true;

    WIFIHAL_TM_START();
    status = wifi_getApAclDevices(ssid->index, acl_buf, sizeof(acl_buf));
    WIFIHAL_TM_STOP_NORET(WIFIHAL_STD_TIME, "1:wifi_getApAclDevices(%d) = %d",
                                  ssid->index, status);
    if (status == RETURN_OK)
    {
        if ((strlen(acl_buf) + 2) > sizeof(acl_buf)) {
            LOGE("%s: ACL List too long for buffer size!", ssid->ifname);
            return false;
        }
        strcat(acl_buf, ",");

        i = 0;
        p = strtok_r(acl_buf, ",\n", &s);
        while (p)
        {
            if (strlen(p) == 0)
            {
                break;
            }
            else if (strlen(p) != 17)
            {
                LOGW("%s: ACL has malformed MAC \"%s\"", ssid->ifname, p);
            }
            else
            {
                strncpy(vconf->mac_list[i], p, sizeof(vconf->mac_list[i])+1);
                i++;
            }

            p = strtok_r(NULL, ",\n", &s);
        }
        vconf->mac_list_len = i;
    }

    return true;
}

bool
wifihal_acl_from_config(wifihal_ssid_t *ssid, struct schema_Wifi_VIF_Config *vconf)
{
    c_item_t                *citem;
    INT                     acl_mode;
    INT                     ret;
    INT                     i;

#ifdef QCA_WIFI
    char                    *unmapped_ifname = target_unmap_ifname(ssid->ifname);

    // !!! XXX: Cannot touch ACL for home interfaces, since they are currently
    //          used for band steering on QCA
    if (!strncmp(unmapped_ifname, "home-ap-", 8)) {
        // Don't touch ACL
        return true;
    }
#endif /* QCA_WIFI */

    // Set ACL type from mac_list_type
    if (vconf->mac_list_type_exists)
    {
        if (!(citem = c_get_item_by_str(map_acl_modes, vconf->mac_list_type)))
        {
            LOGE("%s: Failed to set ACL type (mac_list_type '%s' unknown)",
                 ssid->ifname, vconf->mac_list_type);
            return false;
        }
        acl_mode = (INT)citem->key;

        WIFIHAL_TM_START();
        ret = wifi_setApMacAddressControlMode(ssid->index, acl_mode);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApMacAddressControlMode(%d, %d)",
                                              ssid->index, acl_mode);
        LOGD("[WIFI_HAL SET] wifi_setApMacAddressControlMode(%d, %d) = %d",
                                              ssid->index, acl_mode, ret);
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to set ACL Mode (%d)", ssid->ifname, acl_mode);
            return false;
        }
    }

    /*
     * !!! XXX WAR !!! Currently upper layer only provides changed values.
     * Currently, an array length of 0 could mean two things: it hasn't changed,
     * or it's actually empty.  Since we can't tell the difference at the moment,
     * we have to assume it hasn't changed.
     */
    if (vconf->mac_list_len > 0) {
        // First, flush the table
        WIFIHAL_TM_START();
        ret = wifi_delApAclDevices(ssid->index);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_delApAclDevices(%d)",
                                   ssid->index);
        LOGD("[WIFI_HAL SET] wifi_delApAclDevices(%d) = %d",
                                   ssid->index, ret);

        // Set ACL list
        for (i = 0; i < vconf->mac_list_len; i++)
        {
            WIFIHAL_TM_START();
            ret = wifi_addApAclDevice(ssid->index, vconf->mac_list[i]);
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_addApAclDevice(%d, \"%s\")",
                                      ssid->index, vconf->mac_list[i]);
            LOGD("[WIFI_HAL SET] wifi_addApAclDevice(%d, \"%s\") = %d",
                                      ssid->index, vconf->mac_list[i], ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to add \"%s\" to ACL", ssid->ifname, vconf->mac_list[i]);
            }
        }
    }

    return true;
}
