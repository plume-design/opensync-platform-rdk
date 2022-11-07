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
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/limits.h>

#include "log.h"
#include "const.h"
#include "target.h"
#include "target_internal.h"
#include "util.h"

#define MODULE_ID LOG_MODULE_ID_VIF
#define MAX_MULTI_PSK_KEYS 30

static c_item_t map_enable_disable[] =
{
    C_ITEM_STR(true,                    "enabled"),
    C_ITEM_STR(false,                   "disabled")
};

#define ACL_BUF_SIZE   1024
#define MAX_ACL_NUMBER   64


static c_item_t map_acl_modes[] =
{
    C_ITEM_STR(wifi_mac_filter_mode_white_list, "whitelist"),
    C_ITEM_STR(wifi_mac_filter_mode_black_list, "blacklist")
};

#define DEFAULT_ENC_MODE        "TKIPandAESEncryption"

#define OVSDB_SECURITY_KEY_MGMT_DPP           "dpp"
#define OVSDB_SECURITY_KEY_MGMT_WPA_PSK       "wpa-psk"
#define OVSDB_SECURITY_KEY_MGMT_WPA2_PSK      "wpa2-psk"
#define OVSDB_SECURITY_KEY_MGMT_WPA2_EAP      "wpa2-eap"
#define OVSDB_SECURITY_KEY_MGMT_SAE           "sae"
#define RDK_SECURITY_KEY_MGMT_OPEN            "None"
#define RDK_SECURITY_KEY_MGMT_WPA_PSK         "WPA-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA2_PSK        "WPA2-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA_WPA2_PSK    "WPA-WPA2-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA2_EAP        "WPA2-Enterprise"
#define RDK_SECURITY_KEY_MGMT_WPA3            "WPA3-Sae"
#define RDK_SECURITY_KEY_MGMT_WPA3_TRANSITION "WPA3-Personal-Transition"

static bool ssid_index_to_vap_info(UINT ssid_index, wifi_vap_info_map_t *map, wifi_vap_info_t **vap_info)
{
    UINT i;
    INT radio_idx = -1;

    if (wifi_getSSIDRadioIndex(ssid_index, &radio_idx) != RETURN_OK)
    {
        LOGE("wifi_getSSIDRadioIndex() FAILED ssid_index=%d", ssid_index);
        return false;
    }

    memset(map, 0, sizeof(wifi_vap_info_map_t));

    if (wifi_getRadioVapInfoMap(radio_idx, map) == RETURN_OK)
    {
        for (i = 0; i < map->num_vaps; i++)
        {
            if (map->vap_array[i].vap_index == ssid_index)
            {
                *vap_info = &map->vap_array[i];
                return true;
            }
       }
    }

    LOGE("Cannot find vap_info for ssid_index %d", ssid_index);
    return false;
}

#ifdef WIFI_HAL_VERSION_3_PHASE2
static bool acl_to_state(
        const wifi_vap_info_t *vap_info,
        struct schema_Wifi_VIF_State *vstate)
{
    INT             status = RETURN_ERR;
    mac_address_t   acl_list[MAX_ACL_NUMBER] = {0};
    UINT            acl_number;
    const char      none_mac_list_type[] = "none";
    char            acl_string[MAC_STR_LEN] = {0};
    unsigned int    i;

    if (vap_info->u.bss_info.mac_filter_enable)
    {
        SCHEMA_SET_STR(vstate->mac_list_type,
            c_get_str_by_key(map_acl_modes, vap_info->u.bss_info.mac_filter_mode));
        if (strlen(vstate->mac_list_type) == 0)
        {
            LOGE("%s: Unknown ACL mode (%u)", vap_info->vap_name,
                vap_info->u.bss_info.mac_filter_mode);
            return false;
        }
    }
    else
    {
        SCHEMA_SET_STR(vstate->mac_list_type, none_mac_list_type);
    }

    status = wifi_getApAclDevices(vap_info->vap_index, acl_list, MAX_ACL_NUMBER, &acl_number);
    if (status != RETURN_OK)
    {
        LOGE("%s: Failed to obtain ACL list (status %d)!", vap_info->vap_name, status);
        return false;
    }

    vstate->mac_list_present = true;

    for (i = 0; i < acl_number; i++)
    {
        snprintf(acl_string, sizeof(acl_string), MAC_ADDR_FMT, MAC_ADDR_UNPACK(acl_list[i]));
        SCHEMA_VAL_APPEND(vstate->mac_list, acl_string);
    }

    return true;
}

static bool acl_to_config(const wifi_vap_info_t *vap_info, struct schema_Wifi_VIF_Config *vconf)
{
    INT             status = RETURN_ERR;
    mac_address_t   acl_list[MAX_ACL_NUMBER] = {0};
    UINT            acl_number;
    const char      none_mac_list_type[] = "none";
    char            acl_string[MAC_STR_LEN] = {0};
    unsigned int    i;

    if (vap_info->u.bss_info.mac_filter_enable)
    {
        SCHEMA_SET_STR(vconf->mac_list_type,
            c_get_str_by_key(map_acl_modes, vap_info->u.bss_info.mac_filter_mode));
        if (strlen(vconf->mac_list_type) == 0)
        {
            LOGE("%s: Unknown ACL mode (%u)", vap_info->vap_name,
                vap_info->u.bss_info.mac_filter_mode);
            return false;
        }
    }
    else
    {
        SCHEMA_SET_STR(vconf->mac_list_type, none_mac_list_type);
    }

    status = wifi_getApAclDevices(vap_info->vap_index, acl_list, MAX_ACL_NUMBER, &acl_number);
    if (status != RETURN_OK)
    {
        LOGE("%s: Failed to obtain ACL list (status %d)!", vap_info->vap_name, status);
        return false;
    }

    vconf->mac_list_present = true;

    for (i = 0; i < acl_number; i++)
    {
        snprintf(acl_string, sizeof(acl_string), MAC_ADDR_FMT, MAC_ADDR_UNPACK(acl_list[i]));
        SCHEMA_VAL_APPEND(vconf->mac_list, acl_string);
    }

    return true;
}
#else
static bool acl_to_state(
        const wifi_vap_info_t *vap_info,
        struct schema_Wifi_VIF_State *vstate)
{
    const size_t  mac_str_len = 17;
    const char    mac_list_separator[] = ",\n";
    char          acl_buf[ACL_BUF_SIZE];
    char          *p;
    char          *s = NULL;
    INT           status = RETURN_ERR;
    const char    none_mac_list_type[] = "none";

#ifndef CONFIG_RDK_SYNC_EXT_HOME_ACLS
    // Don't obtain home AP ACLs
    if (is_home_ap(vap_info->vap_name))
    {
        return true;
    }
#endif

    if (vap_info->u.bss_info.mac_filter_enable)
    {
        SCHEMA_SET_STR(vstate->mac_list_type,
            c_get_str_by_key(map_acl_modes, vap_info->u.bss_info.mac_filter_mode));
        if (strlen(vstate->mac_list_type) == 0)
        {
            LOGE("%s: Unknown ACL mode (%u)", vap_info->vap_name,
                vap_info->u.bss_info.mac_filter_mode);
            return false;
        }
    }
    else
    {
        SCHEMA_SET_STR(vstate->mac_list_type, none_mac_list_type);
    }

    memset(acl_buf, 0, sizeof(acl_buf));
    status = wifi_getApAclDevices(vap_info->vap_index, acl_buf, sizeof(acl_buf));
    if (status != RETURN_OK)
    {
        LOGE("%s: Failed to obtain ACL list (status %d)!", vap_info->vap_name, status);
        return false;
    }

    if ((strnlen(acl_buf, sizeof(acl_buf))) == sizeof(acl_buf))
    {
        LOGE("%s: ACL List too long for buffer size!", vap_info->vap_name);
        return false;
    }
    vstate->mac_list_present = true;

    for (p = strtok_r(acl_buf, mac_list_separator, &s);
         p != NULL;
         p = strtok_r(NULL, mac_list_separator, &s))
    {
        if (strlen(p) == 0)
        {
            break;
        }
        else if (strlen(p) != mac_str_len)
        {
            LOGW("%s: ACL has malformed MAC \"%s\"", vap_info->vap_name, p);
        }
        else
        {
            SCHEMA_VAL_APPEND(vstate->mac_list, p);
        }
    }

    return true;
}

static bool acl_to_config(const wifi_vap_info_t *vap_info, struct schema_Wifi_VIF_Config *vconf)
{
    const size_t  mac_str_len = 17;
    const char    mac_list_separator[] = ",\n";
    char          acl_buf[ACL_BUF_SIZE];
    char          *p;
    char          *s = NULL;
    INT           status = RETURN_ERR;
    const char    none_mac_list_type[] = "none";

    if (vap_info->u.bss_info.mac_filter_enable)
    {
        SCHEMA_SET_STR(vconf->mac_list_type,
            c_get_str_by_key(map_acl_modes, vap_info->u.bss_info.mac_filter_mode));
        if (strlen(vconf->mac_list_type) == 0)
        {
            LOGE("%s: Unknown ACL mode (%u)", vap_info->vap_name,
                vap_info->u.bss_info.mac_filter_mode);
            return false;
        }
    }
    else
    {
        SCHEMA_SET_STR(vconf->mac_list_type, none_mac_list_type);
    }

    memset(acl_buf, 0, sizeof(acl_buf));
    status = wifi_getApAclDevices(vap_info->vap_index, acl_buf, sizeof(acl_buf));
    if (status != RETURN_OK)
    {
        LOGE("%s: Failed to obtain ACL list (status %d)!", vap_info->vap_name, status);
        return false;
    }

    if ((strnlen(acl_buf, sizeof(acl_buf))) == sizeof(acl_buf))
    {
        LOGE("%s: ACL List too long for buffer size!", vap_info->vap_name);
        return false;
    }
    vconf->mac_list_present = true;

    for (p = strtok_r(acl_buf, mac_list_separator, &s);
         p != NULL;
         p = strtok_r(NULL, mac_list_separator, &s))
    {
        if (strlen(p) == 0)
        {
            break;
        }
        else if (strlen(p) != mac_str_len)
        {
            LOGW("%s: ACL has malformed MAC \"%s\"", vap_info->vap_name, p);
        }
        else
        {
            SCHEMA_VAL_APPEND(vconf->mac_list, p);
        }
    }

    return true;
}
#endif

static void acl_apply(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_VIF_Config_flags *changed,
        bool *trigger_reconfigure,
        wifi_vap_info_t *vap_info)
{
    INT                          ret;
    INT                          i;
    const c_item_t               *citem = c_get_item_by_str(map_acl_modes, vconf->mac_list_type);
    const char                   none_mac_list_type[] = "none";

#ifndef CONFIG_RDK_SYNC_EXT_HOME_ACLS
    // Don't configure home AP ACLs
    if (is_home_ap(vap_info->vap_name))
    {
        return;
    }
#endif

    // Set ACL type from mac_list_type
    if (changed->mac_list_type && vconf->mac_list_type_exists)
    {
        if (strcmp(none_mac_list_type, vconf->mac_list_type) == 0)
        {
            vap_info->u.bss_info.mac_filter_enable = false;
        }
        else if (citem != NULL)
        {
            vap_info->u.bss_info.mac_filter_enable = true;
            vap_info->u.bss_info.mac_filter_mode = (wifi_mac_filter_mode_t)citem->key;
        }
        else
        {
            LOGW("%s: Failed to set ACL type (mac_list_type '%s' unknown)",
                 vap_info->vap_name, vconf->mac_list_type);
            return;
        }
        *trigger_reconfigure = true;
    }

    if (changed->mac_list)
    {
        // First, flush the table
        ret = wifi_delApAclDevices(ssid_index);
        LOGD("[WIFI_HAL SET] wifi_delApAclDevices(%d) = %d",
                                   ssid_index, ret);

        // Set ACL list
        for (i = 0; i < vconf->mac_list_len; i++)
        {
#ifdef WIFI_HAL_VERSION_3_PHASE2
            mac_address_t mac = {0};

            if (sscanf((char *)vconf->mac_list[i], MAC_ADDR_FMT, MAC_ADDR_UNPACK(&mac)) < (int)sizeof(mac))
            {
                LOGW("%s: Failed to convert ACL %s", vap_info->vap_name, (char *)vconf->mac_list[i]);
                continue;
            }
            ret = wifi_addApAclDevice(ssid_index, mac);
            LOGD("[WIFI_HAL SET] wifi_addApAclDevice(%d, "MAC_ADDR_FMT") = %d",
                                      ssid_index, MAC_ADDR_UNPACK(mac), ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to add "MAC_ADDR_FMT" to ACL", vap_info->vap_name, MAC_ADDR_UNPACK(mac));
            }
#else
            ret = wifi_addApAclDevice(ssid_index, (char *)vconf->mac_list[i]);
            LOGD("[WIFI_HAL SET] wifi_addApAclDevice(%d, \"%s\") = %d",
                                      ssid_index, vconf->mac_list[i], ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to add \"%s\" to ACL", vap_info->vap_name, vconf->mac_list[i]);
            }
#endif
        }
    }
}

static bool security_key_mgmt_hal_to_ovsdb(
        wifi_security_modes_t mode,
        struct schema_Wifi_VIF_State *vstate,
        struct schema_Wifi_VIF_Config *vconfig)
{
    switch (mode)
    {
        case wifi_security_mode_wpa_personal:
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
            return true;
        case wifi_security_mode_wpa2_personal:
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
            return true;
        case wifi_security_mode_wpa_wpa2_personal:
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
            return true;
        case wifi_security_mode_wpa3_personal:
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
            return true;
        case wifi_security_mode_wpa3_transition:
            if (vstate)
            {
                SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
                SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
            }
            if (vconfig)
            {
                SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
                SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
            }
            return true;
        case wifi_security_mode_wpa2_enterprise:
            // The only 'enterprise' encryption present in OVSDB schema is WPA2-Enterprise, so skip
            // other RDK 'enterprise' types.
            if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP);
            if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP);
            return true;
        default:
            LOGW("%s: unsupported security key mgmt (%d)", __func__, mode);
            break;
    }

    return false;
}

static bool get_enterprise_credentials(
        wifi_radius_settings_t radius,
        struct schema_Wifi_VIF_State *vstate)
{
    // Keep it as a function because for HAL_VERSION_3_PHASE2
    // we need to translate ip_addr_t into string here (TODO).
    SCHEMA_SET_STR(vstate->radius_srv_addr, (const char *)radius.ip);
    SCHEMA_SET_INT(vstate->radius_srv_port, radius.port);
    SCHEMA_SET_STR(vstate->radius_srv_secret, radius.key);

    return true;
}

static bool get_psks(
        INT ssid_index,
        wifi_security_key_t key,
        struct schema_Wifi_VIF_State *vstate)
{
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    INT ret;
    wifi_key_multi_psk_t keys[MAX_MULTI_PSK_KEYS];
    int i;
#endif

    if (strlen(key.key) == 0)
    {
        LOGW("Empty psk! VAP index=%d",
              ssid_index);
    }

    SCHEMA_KEY_VAL_APPEND(vstate->wpa_psks, cached_key_ids[ssid_index], key.key);
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    memset(keys, 0, sizeof(keys));
    LOGT("wifi_getMultiPskKeys() index=%d", ssid_index);
    ret = wifi_getMultiPskKeys(ssid_index, keys, MAX_MULTI_PSK_KEYS);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getMultiPskKeys() FAILED index=%d", ssid_index);
        return false;
    }
    LOGT("wifi_getMultiPskKeys() OK index=%d", ssid_index);

    for (i = 0; i < MAX_MULTI_PSK_KEYS; i++)
    {
         if (strlen(keys[i].wifi_keyId) && strlen(keys[i].wifi_psk))
         {
             SCHEMA_KEY_VAL_APPEND(vstate->wpa_psks, keys[i].wifi_keyId, keys[i].wifi_psk);
         }
    }
#endif

    return true;
}

static bool get_security(
        INT ssidIndex,
        wifi_vap_info_t *vap_info,
        struct schema_Wifi_VIF_State *vstate)
{
    wifi_security_modes_t mode = vap_info->u.bss_info.security.mode;

    if (mode == wifi_security_mode_none)
    {
        SCHEMA_SET_INT(vstate->wpa, 0);
        return true;
    }

    SCHEMA_SET_INT(vstate->wpa, 1);
    if (!security_key_mgmt_hal_to_ovsdb(mode, vstate, NULL)) return false;

    // The only 'enterprise' encryption present in OVSDB schema is WPA2-Enterprise, so skip
    // other RDK 'enterprise' types.
    if (mode == wifi_security_mode_wpa2_enterprise)
    {
        return get_enterprise_credentials(vap_info->u.bss_info.security.u.radius, vstate);
    }

    return get_psks(ssidIndex, vap_info->u.bss_info.security.u.key, vstate);
}

#ifdef CONFIG_RDK_DISABLE_SYNC
#define MAX_MODE_LEN         25
#define MAX_PASS_LEN         65
/**
 * Mesh Sync Wifi configuration change message
 */
typedef struct _MeshWifiAPSecurity {
    uint32_t  index;                    // AP index [0-15]
    char      passphrase[MAX_PASS_LEN]; // AP Passphrase
    char      secMode[MAX_MODE_LEN];    // Security mode
    char      encryptMode[MAX_MODE_LEN];    // Encryption mode
} MeshWifiAPSecurity;

#endif

static bool security_wpa_key_mgmt_match(const struct schema_Wifi_VIF_Config *vconf,
                            const char *key_mgmt)
{
    int i = 0;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        if (strstr(vconf->wpa_key_mgmt[i], key_mgmt))
            return true;
    }

    return false;
}

static bool security_key_mgmt_ovsdb_to_hal(const struct schema_Wifi_VIF_Config *vconf, wifi_security_modes_t *mode)
{
    /* Only key mgmt modes combinations that can be reflected in RDK HAL API
     * are handled.
     * Note: WEP is not supported in ovsdb at all.
     */
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA_PSK) &&
            security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK))
    {
        *mode = wifi_security_mode_wpa_wpa2_personal;
    }
    else if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK) &&
                 security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_SAE))
    {
        *mode = wifi_security_mode_wpa3_transition;
    }
    else if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA_PSK))
    {
        *mode = wifi_security_mode_wpa_personal;
    }
    else if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK))
    {
        *mode = wifi_security_mode_wpa2_personal;
    }
    else if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP))
    {
        *mode = wifi_security_mode_wpa_wpa2_personal;
    }
    else if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_SAE))
    {
        *mode = wifi_security_mode_wpa3_personal;
    }
    else
    {
        LOGW("%s: unsupported security key mgmt!", __func__);
        return false;
    }

    return true;
}

#if !defined(CONFIG_RDK_DISABLE_SYNC) && !defined(CONFIG_RDK_MULTI_PSK_SUPPORT)
static const char *security_key_mgmt_ovsdb_to_sync(const struct schema_Wifi_VIF_Config *vconf)
{
    /* Only key mgmt modes combinations that can be reflected in RDK HAL API
     * are handled.
     * Note: WEP is not supported in ovsdb at all.
     */
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA_PSK) &&
            security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK))
    {
        return RDK_SECURITY_KEY_MGMT_WPA_WPA2_PSK;
    }
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK) &&
            security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_SAE))
    {
        return RDK_SECURITY_KEY_MGMT_WPA3_TRANSITION;
    }
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA_PSK))
    {
        return RDK_SECURITY_KEY_MGMT_WPA_PSK;
    }
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK))
    {
        return RDK_SECURITY_KEY_MGMT_WPA2_PSK;
    }
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP))
    {
        return RDK_SECURITY_KEY_MGMT_WPA2_EAP;
    }
    if (security_wpa_key_mgmt_match(vconf, OVSDB_SECURITY_KEY_MGMT_SAE))
    {
        return RDK_SECURITY_KEY_MGMT_WPA3;
    }

    LOGW("%s: unsupported security key mgmt!", __func__);
    return NULL;
}

static bool security_ovsdb_to_syncmsg(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        MeshWifiAPSecurity *dest)
{
    const char *mode = security_key_mgmt_ovsdb_to_sync(vconf);

    if (!mode) return false;
    STRSCPY(dest->secMode, mode);

    STRSCPY(dest->passphrase, vconf->wpa_psks[0]); // MeshAgent doesn't support Multi-PSK
    STRSCPY(dest->encryptMode, DEFAULT_ENC_MODE);

    dest->index = ssid_index;

    return true;
}
#endif

bool vif_external_ssid_update(const char *ssid, int ssid_index)
{
    INT ret;
    int radio_idx;
    char radio_ifname[128];
    char ssid_ifname[128];
    struct schema_Wifi_VIF_Config vconf;

    memset(&vconf, 0, sizeof(vconf));
    vconf._partial_update = true;

    memset(ssid_ifname, 0, sizeof(ssid_ifname));
    ret = wifi_getApName(ssid_index, ssid_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get ap name for index %d", __func__, ssid_index);
        return false;
    }

    ret = wifi_getSSIDRadioIndex(ssid_index, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio idx for SSID %s\n", __func__, ssid);
        return false;
    }

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radio_idx, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio ifname for idx %d", __func__,
                radio_idx);
        return false;
    }

    SCHEMA_SET_STR(vconf.if_name, target_unmap_ifname(ssid_ifname));
    SCHEMA_SET_STR(vconf.ssid, ssid);

    radio_rops_vconfig(&vconf, radio_ifname);

    return true;
}

bool vif_external_security_update(int ssid_index)
{
    int radio_index;
    INT ret;
    char radio_ifname[128];
    struct schema_Wifi_VIF_Config vconf;
    wifi_vap_info_map_t vap_info_map;
    wifi_vap_info_t *vap_info = NULL;
    wifi_security_modes_t mode;

    memset(&vconf, 0, sizeof(vconf));
    vconf._partial_update = true;

    if (!ssid_index_to_vap_info((UINT)ssid_index, &vap_info_map, &vap_info)) return false;

    SCHEMA_SET_STR(vconf.if_name, target_unmap_ifname(vap_info->vap_name));

    ret = wifi_getSSIDRadioIndex(ssid_index, &radio_index);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio idx for SSID %s", __func__, vconf.if_name);
        return false;
    }

    if (radio_index < 0)
    {
        LOGE("%s: wrong radio index (%d) for VAP %s\b", __func__, radio_index, vconf.if_name);
        return false;
    }

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radio_index, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio ifname for idx %d", __func__,
                radio_index);
        return false;
    }

    mode = vap_info->u.bss_info.security.mode;

    if (mode == wifi_security_mode_none)
    {
        SCHEMA_SET_INT(vconf.wpa, 0);
        SCHEMA_UNSET_MAP(vconf.wpa_key_mgmt);
        SCHEMA_UNSET_MAP(vconf.wpa_psks);
    }
    else
    {
         if (!security_key_mgmt_hal_to_ovsdb(mode, NULL, &vconf)) return false;

         SCHEMA_SET_INT(vconf.wpa, 1);
         SCHEMA_KEY_VAL_APPEND(vconf.wpa_psks, cached_key_ids[ssid_index], vap_info->u.bss_info.security.u.key.key);
    }

    LOGD("Updating VIF for new security");
    radio_rops_vconfig(&vconf, radio_ifname);

    return true;
}

static void copy_acl_to_config(struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_VIF_State *vstate)
{
    int i;
    SCHEMA_CPY_STR(vconf->mac_list_type, vstate->mac_list_type);
    vconf->mac_list_present = vstate->mac_list_present;
    for (i = 0; i < vstate->mac_list_len; i++)
    {
        SCHEMA_VAL_APPEND(vconf->mac_list, vstate->mac_list[i]);
    }
}

bool vif_external_acl_update(INT ssid_index)
{
    INT ret;
    char radio_ifname[WIFIHAL_MAX_BUFFER];
    struct schema_Wifi_VIF_Config vconf;
    wifi_vap_info_map_t vap_info_map;
    wifi_vap_info_t *vap_info = NULL;

    if (!ssid_index_to_vap_info((UINT)ssid_index, &vap_info_map, &vap_info)) return false;

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(vap_info->radio_index, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio ifname for idx %u", __func__, vap_info->radio_index);
        return false;
    }

    memset(&vconf, 0, sizeof(vconf));
    if (!acl_to_config(vap_info, &vconf)) return false;
    SCHEMA_SET_STR(vconf.if_name, vap_info->vap_name);
    vconf._partial_update = true;

    return radio_rops_vconfig(&vconf, radio_ifname);
}

bool vif_copy_to_config(
        INT ssidIndex,
        struct schema_Wifi_VIF_State *vstate,
        struct schema_Wifi_VIF_Config *vconf)
{
    int i;

    memset(vconf, 0, sizeof(*vconf));
    vconf->_partial_update = true;

    SCHEMA_SET_STR(vconf->if_name, vstate->if_name);
    LOGT("vconf->ifname = %s", vconf->if_name);
    SCHEMA_SET_STR(vconf->mode, vstate->mode);
    LOGT("vconf->mode = %s", vconf->mode);
    SCHEMA_SET_INT(vconf->enabled, vstate->enabled);
    LOGT("vconf->enabled = %d", vconf->enabled);
    if (vstate->bridge_exists)
    {
        SCHEMA_SET_STR(vconf->bridge, vstate->bridge);
    }
    LOGT("vconf->bridge = %s", vconf->bridge);
    SCHEMA_SET_INT(vconf->ap_bridge, vstate->ap_bridge);
    LOGT("vconf->ap_bridge = %d", vconf->ap_bridge);
    SCHEMA_SET_INT(vconf->wds, vstate->wds);
    LOGT("vconf->wds = %d", vconf->wds);
    SCHEMA_SET_STR(vconf->ssid_broadcast, vstate->ssid_broadcast);
    LOGT("vconf->ssid_broadcast = %s", vconf->ssid_broadcast);
    SCHEMA_SET_STR(vconf->ssid, vstate->ssid);
    LOGT("vconf->ssid = %s", vconf->ssid);
    SCHEMA_SET_INT(vconf->rrm, vstate->rrm);
    LOGT("vconf->rrm = %d", vconf->rrm);
    SCHEMA_SET_INT(vconf->btm, vstate->btm);
    LOGT("vconf->btm = %d", vconf->btm);
    if (vconf->uapsd_enable_exists)
    {
        SCHEMA_SET_INT(vconf->uapsd_enable, vstate->uapsd_enable);
        LOGT("vconf->uapsd_enable = %d", vconf->uapsd_enable);
    }
    if (vconf->wps_exists)
    {
        SCHEMA_SET_INT(vconf->wps, vstate->wps);
        LOGT("vconf->wps = %d", vconf->wps);
    }
    if (vconf->wps_pbc_exists)
    {
        SCHEMA_SET_INT(vconf->wps_pbc, vstate->wps_pbc);
        LOGT("vconf->wps_pbc = %d", vconf->wps_pbc);
    }
    if (vconf->wps_pbc_key_id_exists)
    {
        SCHEMA_SET_STR(vconf->wps_pbc_key_id, vstate->wps_pbc_key_id);
        LOGT("vconf->wps_pbc_key_id = %s", vconf->wps_pbc_key_id);
    }

    SCHEMA_SET_INT(vconf->wpa, vstate->wpa);
    LOGT("vconf->wpa = %d", vconf->wpa);

    for (i = 0; i < vstate->wpa_key_mgmt_len; i++)
    {
        SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, vstate->wpa_key_mgmt[i]);
        LOGT("vconf->wpa_key_mgmt[%d] = %s", i, vconf->wpa_key_mgmt[i]);
    }

    for (i = 0; i < vstate->wpa_psks_len; i++)
    {
        SCHEMA_KEY_VAL_APPEND(vconf->wpa_psks, vstate->wpa_psks_keys[i],
                vstate->wpa_psks[i]);
    }

    if (vstate->radius_srv_addr_exists)
    {
        SCHEMA_SET_STR(vconf->radius_srv_addr, vstate->radius_srv_addr);
        LOGT("vconf->radius_srv_addr = %s", vconf->radius_srv_addr);
    }

    if (vstate->radius_srv_port_exists)
    {
        SCHEMA_SET_INT(vconf->radius_srv_port, vstate->radius_srv_port);
        LOGT("vconf->radius_srv_port = %d", vconf->radius_srv_port);
    }

    if (vstate->radius_srv_secret_exists)
    {
        SCHEMA_SET_STR(vconf->radius_srv_secret, vstate->radius_srv_secret);
    }

    if (vconf->mcast2ucast_exists)
    {
        SCHEMA_SET_INT(vconf->mcast2ucast, vstate->mcast2ucast);
        LOGT("vconf->mcast2ucast = %d", vconf->mcast2ucast);
    }

    copy_acl_to_config(vconf, vstate);

    return true;
}

bool vif_get_radio_ifname(
        INT ssidIndex,
        char *radio_ifname,
        size_t radio_ifname_size)
{
    INT ret;
    INT radio_idx;

    ret = wifi_getSSIDRadioIndex(ssidIndex, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio idx for SSID index %d\n", __func__, ssidIndex);
        return false;
    }

    if (radio_ifname_size != 0 && radio_ifname != NULL)
    {
        memset(radio_ifname, 0, radio_ifname_size);
        ret = wifi_getRadioIfName(radio_idx, radio_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get radio ifname for idx %d", __func__,
                    radio_idx);
            return false;
        }
        strscpy(radio_ifname, target_unmap_ifname(radio_ifname), radio_ifname_size);
    }
    return true;
}

static bool get_channel(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    INT radio_idx = -1;
    wifi_radio_operationParam_t radio_params;

    LOGT("wifi_getSSIDRadioIndex() index=%d", ssidIndex);
    ret = wifi_getSSIDRadioIndex(ssidIndex, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getSSIDRadioIndex() FAILED index=%d", ssidIndex);
        return false;
    }
    LOGT("wifi_getSSIDRadioIndex() OK index=%d radio_idx=%d", ssidIndex, radio_idx);

    memset(&radio_params, 0, sizeof(radio_params));
    LOGT("wifi_getRadioOperatingParameters() radio_index=%d", radio_idx);
    ret = wifi_getRadioOperatingParameters(radio_idx, &radio_params);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getRadioOperatingParameters() FAILED radio_idx=%d ret=%d", radio_idx, ret);
        return false;
    }
    LOGT("wifi_getRadioOperatingParameters() OK radio_idx=%d channel=%u", radio_idx, radio_params.channel);

    SCHEMA_SET_INT(vstate->channel, radio_params.channel);
    return true;
}

bool vif_state_get(
        INT ssidIndex,
        struct schema_Wifi_VIF_State *vstate)
{
    memset(vstate, 0, sizeof(*vstate));
    vstate->_partial_update = true;
    vstate->associated_clients_present = false;
    vstate->vif_config_present = false;
    wifi_vap_info_map_t vap_info_map;
    wifi_vap_info_t *vap_info = NULL;
    char *str = NULL;
    char mac_str[sizeof(vstate->mac)];

    if (ssidIndex < 0)
    {
        LOGE("Negative ssidIndex: %d", ssidIndex);
        return false;
    }

    if (!ssid_index_to_vap_info((UINT)ssidIndex, &vap_info_map, &vap_info)) return false;

    SCHEMA_SET_STR(vstate->if_name, target_unmap_ifname(vap_info->vap_name));

    if (get_channel(ssidIndex, vstate) != true) return false;

    SCHEMA_SET_INT(vstate->enabled, vap_info->u.bss_info.enabled);
    SCHEMA_SET_STR(vstate->mode, "ap");
    SCHEMA_SET_INT(vstate->wds, false);

    SCHEMA_SET_STR(vstate->ssid, vap_info->u.bss_info.ssid);
    SCHEMA_SET_INT(vstate->ap_bridge, vap_info->u.bss_info.isolation ? false : true);

    str = c_get_str_by_key(map_enable_disable, vap_info->u.bss_info.showSsid);
    if (strlen(str) == 0)
    {
        LOGW("Failed to decode ssid_broadcast index=%d val=%d", ssidIndex,
            vap_info->u.bss_info.showSsid);
        return false;
    }
    SCHEMA_SET_STR(vstate->ssid_broadcast, str);

    memset(mac_str, 0, sizeof(mac_str));
    snprintf(mac_str, sizeof(mac_str), MAC_ADDRESS_FORMAT,
        MAC_ADDRESS_PRINT(vap_info->u.bss_info.bssid));
    SCHEMA_SET_STR(vstate->mac, mac_str);

    SCHEMA_SET_INT(vstate->rrm, vap_info->u.bss_info.nbrReportActivated);
    SCHEMA_SET_INT(vstate->btm, vap_info->u.bss_info.bssTransitionActivated);
    SCHEMA_SET_INT(vstate->uapsd_enable, vap_info->u.bss_info.UAPSDEnabled);

    if (strlen(vap_info->bridge_name) > 0)
    {
        SCHEMA_SET_STR(vstate->bridge, vap_info->bridge_name);
        LOGT("vstate->bridge set to '%s'", vstate->bridge);
    }
    else
    {
        vstate->bridge_exists = false;
    }

    get_security(ssidIndex, vap_info, vstate);
    acl_to_state(vap_info, vstate);

#ifdef CONFIG_RDK_WPS_SUPPORT
    wps_to_state(ssidIndex, vstate);
#endif

#ifdef CONFIG_RDK_MULTI_AP_SUPPORT
    multi_ap_to_state(ssidIndex, vstate);
#endif

    // We currently do not support mcast2ucast feature on RDK platforms
    SCHEMA_SET_INT(vstate->mcast2ucast, false);

    return true;
}

static bool set_password(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        wifi_vap_info_t *vap_info)
{
    STRSCPY(vap_info->u.bss_info.security.u.key.key, vconf->wpa_psks[0]);
    STRSCPY(cached_key_ids[ssid_index], vconf->wpa_psks_keys[0]);
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    if (vconf->wpa_psks_len > 1)
    {
        wifi_key_multi_psk_t *keys = NULL;
        int i;
        INT ret;

        keys = CALLOC(vconf->wpa_psks_len - 1, sizeof(wifi_key_multi_psk_t));

        for (i = 0; i < vconf->wpa_psks_len - 1; i++)
        {
            STRSCPY(keys[i].wifi_keyId, vconf->wpa_psks_keys[i + 1]);
            STRSCPY(keys[i].wifi_psk, vconf->wpa_psks[i + 1]);
            // MAC set to 00:00:00:00:00:00
        }
        LOGT("wifi_pushMultiPskKeys() index=%d", ssid_index);
        ret = wifi_pushMultiPskKeys(ssid_index, keys, vconf->wpa_psks_len - 1);
        FREE(keys);
        if (ret != RETURN_OK)
        {
            LOGW("wifi_pushMultiPskKeys() FAILED index=%d", ssid_index);
            return false;
        }
        LOGT("wifi_pushMultiPskKeys() OK index=%d", ssid_index);
    }
    else
    {
        // Clean multi-psk keys
        INT ret;
        ret = wifi_pushMultiPskKeys(ssid_index, NULL, 0);
        if (ret != RETURN_OK)
        {
            LOGW("wifi_pushMultiPskKeys() FAILED index=%d (cleaning)", ssid_index);
            return false;
        }
    }
#endif

    return true;
}

static void set_security(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_VIF_Config_flags *changed,
        bool *trigger_reconfig,
        wifi_vap_info_t *vap_info)
{
    bool send_sync = false;
    wifi_security_modes_t mode;

    if (changed->wpa && vconf->wpa == 0)
    {
        vap_info->u.bss_info.security.mode = wifi_security_mode_none;
        send_sync = true;
        *trigger_reconfig = true;
        goto exit;
    }

    if (changed->wpa_key_mgmt)
    {
        if (security_key_mgmt_ovsdb_to_hal(vconf, &mode) == false)
        {
            LOGE("Failed to decode security mode for AP index: %d", ssid_index);
            return;
        }
        vap_info->u.bss_info.security.mode = mode;
        send_sync = true;
        *trigger_reconfig = true;
    }

    if (changed->wpa_psks && vconf->wpa_psks_len >= 1)
    {
        if (!set_password(ssid_index, vconf, vap_info)) goto exit;
        send_sync = true;
        *trigger_reconfig = true;
    }

exit:
    if (send_sync)
    {
#if !defined(CONFIG_RDK_DISABLE_SYNC) && !defined(CONFIG_RDK_MULTI_PSK_SUPPORT)
        MeshWifiAPSecurity mesh_security_data;

        memset(&mesh_security_data, 0, sizeof(mesh_security_data));
        // Prepare sync message in case it needs to be updated and sent
        security_ovsdb_to_syncmsg(ssid_index, vconf, &mesh_security_data);

        if (changed->wpa && vconf->wpa == 0)
        {
            STRSCPY(mesh_security_data.secMode, RDK_SECURITY_KEY_MGMT_OPEN);
        }

        STRSCPY(mesh_security_data.passphrase, vconf->wpa_psks[0]);
        if (!sync_send_security_change(ssid_index, vconf->if_name, &mesh_security_data))
        {
            LOGW("%s: Failed to sync security change", vconf->if_name);
        }
#endif
    }
}

bool vif_ifname_to_idx(const char *ifname, INT *outSsidIndex)
{
    INT ret;
    ULONG s, snum;
    INT ssid_index = -1;
    char ssid_ifname[128];

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        return false;
    }

    for (s = 0; s < snum; s++)
    {
        memset(ssid_ifname, 0, sizeof(ssid_ifname));
        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: cannot get ap name for index %ld", __func__, s);
            return false;
        }

        if (!strcmp(ssid_ifname, ifname))
        {
            ssid_index = s;
            break;
        }
    }

    if (ssid_index == -1)
    {
        LOGE("%s: cannot find SSID index for %s", __func__, ifname);
        return false;
    }

    *outSsidIndex = ssid_index;
    return true;
}

bool target_vif_config_set2(
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_Radio_Config *rconf,
        const struct schema_Wifi_Credential_Config *cconfs,
        const struct schema_Wifi_VIF_Config_flags *changed,
        int num_cconfs)
{
    INT ssid_index;
    wifi_vap_info_map_t vap_info_map_current;
    wifi_vap_info_map_t vap_info_map_desired;
    wifi_vap_info_t *vap_info = NULL;
    bool trigger_reconfig = false;
    c_item_t *citem = NULL;

    if (!vif_ifname_to_idx(target_map_ifname((char *)vconf->if_name), &ssid_index))
    {
        LOGE("%s: cannot get index for %s", __func__, target_map_ifname((char *)vconf->if_name));
        return false;
    }

    if (!ssid_index_to_vap_info((UINT)ssid_index, &vap_info_map_current, &vap_info)) return false;

    if (changed->enabled)
    {
        vap_info->u.bss_info.enabled = vconf->enabled;
        trigger_reconfig = true;
    }

    set_security(ssid_index, vconf, changed, &trigger_reconfig, vap_info);

    if (changed->ap_bridge)
    {
        vap_info->u.bss_info.isolation = !vconf->ap_bridge;
        trigger_reconfig = true;
    }
    if (changed->rrm)
    {
        vap_info->u.bss_info.nbrReportActivated = vconf->rrm;
        trigger_reconfig = true;
    }
    if (changed->btm)
    {
        vap_info->u.bss_info.bssTransitionActivated = vconf->btm;
        trigger_reconfig = true;
    }
    if (changed->uapsd_enable)
    {
        vap_info->u.bss_info.UAPSDEnabled = vconf->uapsd_enable;
        trigger_reconfig = true;
    }
    if (changed->ssid_broadcast)
    {
        citem = c_get_item_by_str(map_enable_disable, vconf->ssid_broadcast);
        if (!citem)
        {
            LOGE("%s: Unknown SSID broadcast option \"%s\"!", vconf->if_name, vconf->ssid_broadcast);
            return false;
        }
        vap_info->u.bss_info.showSsid = (BOOL)citem->key;
        trigger_reconfig = true;
#ifndef CONFIG_RDK_DISABLE_SYNC
        if (!sync_send_ssid_broadcast_change(ssid_index, vap_info->u.bss_info.showSsid))
        {
            LOGW("%s: Failed to sync SSID Broadcast change to %s", vconf->if_name, vconf->ssid_broadcast);
        }
#endif
    }
    if (changed->ssid)
    {
        STRSCPY(vap_info->u.bss_info.ssid, vconf->ssid);
        trigger_reconfig = true;
        LOGI("%s: SSID updated to '%s'", vconf->if_name, vconf->ssid);
#ifndef CONFIG_RDK_DISABLE_SYNC
        if (!sync_send_ssid_change(ssid_index, vconf->if_name, vconf->ssid))
        {
            LOGE("%s: Failed to sync SSID change to '%s'", vconf->if_name, vconf->ssid);
        }
#endif
    }
    if (changed->bridge)
    {
        STRSCPY(vap_info->bridge_name, vconf->bridge);
        trigger_reconfig = true;
    }

    acl_apply(ssid_index, vconf, changed, &trigger_reconfig, vap_info);

#ifdef CONFIG_RDK_WPS_SUPPORT
    vif_config_set_wps(ssid_index, vconf, changed, rconf->if_name);
#endif

#ifdef CONFIG_RDK_MULTI_AP_SUPPORT
    vif_config_set_multi_ap(ssid_index, vconf->multi_ap, changed);
#endif

    if (trigger_reconfig)
    {
        memset(&vap_info_map_desired, 0, sizeof(vap_info_map_desired));
        vap_info_map_desired.num_vaps = 1;
        memcpy(&vap_info_map_desired.vap_array[0], vap_info, sizeof(wifi_vap_info_t));
        if (wifi_createVAP(vap_info->radio_index, &vap_info_map_desired) != RETURN_OK)
        {
            LOGW("Failed to apply SSID settings for index=%d", ssid_index);
        }
    }

    return vif_state_update(ssid_index);
}

bool vif_state_update(INT ssidIndex)
{
    struct schema_Wifi_VIF_State vstate;
    char radio_ifname[128];

    if (!vif_state_get(ssidIndex, &vstate))
    {
        LOGE("%s: cannot update VIF state for SSID index %d", __func__, ssidIndex);
        return false;
    }

    if (!vif_get_radio_ifname(ssidIndex, radio_ifname, sizeof(radio_ifname)))
    {
        LOGE("%s: cannot get radio ifname for SSID index %d", __func__, ssidIndex);
        return false;
    }

    LOGN("Updating VIF state for SSID index %d", ssidIndex);
    return radio_rops_vstate(&vstate, radio_ifname);
}

bool is_home_ap(const char *ifname)
{
    return strcmp(ifname, CONFIG_RDK_HOME_AP_24_IFNAME) == 0 ||
           strcmp(ifname, CONFIG_RDK_HOME_AP_50_IFNAME) == 0 ||
           strcmp(ifname, CONFIG_RDK_HOME_AP_60_IFNAME) == 0;
}

