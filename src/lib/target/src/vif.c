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
#include "evsched.h"
#include "util.h"

#define MODULE_ID LOG_MODULE_ID_VIF
#define MAX_MULTI_PSK_KEYS 30

static c_item_t map_enable_disable[] =
{
    C_ITEM_STR(true,                    "enabled"),
    C_ITEM_STR(false,                   "disabled")
};

#define ACL_BUF_SIZE   1024

enum
{
    WEXT_ACL_MODE_DISABLE       = 0,
    WEXT_ACL_MODE_WHITELIST,
    WEXT_ACL_MODE_BLACKLIST,
    WEXT_ACL_MODE_FLUSH
};

static c_item_t map_acl_modes[] =
{
    C_ITEM_STR(WEXT_ACL_MODE_DISABLE,   "none"),
    C_ITEM_STR(WEXT_ACL_MODE_WHITELIST, "whitelist"),
    C_ITEM_STR(WEXT_ACL_MODE_BLACKLIST, "blacklist"),
    C_ITEM_STR(WEXT_ACL_MODE_FLUSH,     "flush")
};

#define DEFAULT_ENC_MODE        "TKIPandAESEncryption"

#define OVSDB_SECURITY_KEY                  "key"
#define OVSDB_SECURITY_OFTAG                "oftag"
#define OVSDB_SECURITY_MODE                 "mode"
#   define OVSDB_SECURITY_MODE_WEP64           "64"
#   define OVSDB_SECURITY_MODE_WEP128          "128"
#   define OVSDB_SECURITY_MODE_WPA1            "1"
#   define OVSDB_SECURITY_MODE_WPA2            "2"
#   define OVSDB_SECURITY_MODE_MIXED           "mixed"
#define OVSDB_SECURITY_ENCRYPTION           "encryption"
#   define OVSDB_SECURITY_ENCRYPTION_OPEN      "OPEN"
#   define OVSDB_SECURITY_ENCRYPTION_WEP       "WEP"
#   define OVSDB_SECURITY_ENCRYPTION_WPA_PSK   "WPA-PSK"
#   define OVSDB_SECURITY_ENCRYPTION_WPA_EAP   "WPA-EAP"
#define OVSDB_SECURITY_RADIUS_SERVER_IP     "radius_server_ip"
#define OVSDB_SECURITY_RADIUS_SERVER_PORT   "radius_server_port"
#define OVSDB_SECURITY_RADIUS_SERVER_SECRET "radius_server_secret"

typedef enum
{
    SEC_NONE                = 0,
    SEC_WEP_64,
    SEC_WEP_128,
    SEC_WPA_PERSONAL,
    SEC_WPA_ENTERPRISE,
    SEC_WPA2_PERSONAL,
    SEC_WPA2_ENTERPRISE,
    SEC_WPA_WPA2_PERSONAL,
    SEC_WPA_WPA2_ENTERPRISE
} sec_type_t;

static c_item_t map_security[] =
{
    C_ITEM_STR(SEC_NONE,                    "None"),
    C_ITEM_STR(SEC_WEP_64,                  "WEP-64"),
    C_ITEM_STR(SEC_WEP_128,                 "WEP-128"),
    C_ITEM_STR(SEC_WPA_PERSONAL,            "WPA-Personal"),
    C_ITEM_STR(SEC_WPA_ENTERPRISE,          "WPA-Enterprise"),
    C_ITEM_STR(SEC_WPA2_PERSONAL,           "WPA2-Personal"),
    C_ITEM_STR(SEC_WPA2_ENTERPRISE,         "WPA2-Enterprise"),
    C_ITEM_STR(SEC_WPA_WPA2_PERSONAL,       "WPA-WPA2-Personal"),
    C_ITEM_STR(SEC_WPA_WPA2_ENTERPRISE,     "WPA-WPA2-Enterprise")
};

static bool acl_to_state(
        INT ssid_index,
        char *ssid_ifname,
        struct schema_Wifi_VIF_State *vstate)
{
    char                    acl_buf[ACL_BUF_SIZE];
    char                    *p;
    char                    *s = NULL;
    INT                     acl_mode;
    INT                     status = RETURN_ERR;
    INT                     i;

    status = wifi_getApMacAddressControlMode(ssid_index, &acl_mode);
    if (status != RETURN_OK)
    {
        LOGE("%s: Failed to get ACL mode", ssid_ifname);
        return false;
    }

    STRSCPY(vstate->mac_list_type,
            c_get_str_by_key(map_acl_modes, acl_mode));
    if (strlen(vstate->mac_list_type) == 0)
    {
        LOGE("%s: Unknown ACL mode (%u)", ssid_ifname, acl_mode);
        return false;
    }
    vstate->mac_list_type_exists = true;

    memset(acl_buf, 0, sizeof(acl_buf));
    status = wifi_getApAclDevices(ssid_index, acl_buf, sizeof(acl_buf));
    if (status == RETURN_OK)
    {
        if ((strlen(acl_buf) + 2) > sizeof(acl_buf))
        {
            LOGE("%s: ACL List too long for buffer size!", ssid_ifname);
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
                LOGW("%s: ACL has malformed MAC \"%s\"", ssid_ifname, p);
            }
            else
            {
                STRSCPY(vstate->mac_list[i], p);
                i++;
            }

            p = strtok_r(NULL, ",\n", &s);
        }
        vstate->mac_list_len = i;
    }

    return true;
}

static void acl_apply(
        INT ssid_index,
        const char *ssid_ifname,
        const struct schema_Wifi_VIF_Config *vconf)
{
    c_item_t                *citem;
    INT                     acl_mode;
    INT                     ret;
    INT                     i;

    // !!! XXX: Cannot touch ACL for home interfaces, since they are currently
    //          used for band steering.
    if (!strncmp(ssid_ifname, "home-ap-", 8))
    {
        // Don't touch ACL
        return;
    }

    // Set ACL type from mac_list_type
    if (vconf->mac_list_type_exists)
    {
        if (!(citem = c_get_item_by_str(map_acl_modes, vconf->mac_list_type)))
        {
            LOGW("%s: Failed to set ACL type (mac_list_type '%s' unknown)",
                 ssid_ifname, vconf->mac_list_type);
            return;
        }
        acl_mode = (INT)citem->key;

        ret = wifi_setApMacAddressControlMode(ssid_index, acl_mode);
        LOGD("[WIFI_HAL SET] wifi_setApMacAddressControlMode(%d, %d) = %d",
                                              ssid_index, acl_mode, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to set ACL Mode (%d)", ssid_ifname, acl_mode);
            return;
        }
    }

    if (vconf->mac_list_len > 0)
    {
        // First, flush the table
        ret = wifi_delApAclDevices(ssid_index);
        LOGD("[WIFI_HAL SET] wifi_delApAclDevices(%d) = %d",
                                   ssid_index, ret);

        // Set ACL list
        for (i = 0; i < vconf->mac_list_len; i++)
        {
            ret = wifi_addApAclDevice(ssid_index, (char *)vconf->mac_list[i]);
            LOGD("[WIFI_HAL SET] wifi_addApAclDevice(%d, \"%s\") = %d",
                                      ssid_index, vconf->mac_list[i], ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to add \"%s\" to ACL", ssid_ifname, vconf->mac_list[i]);
            }
        }
    }
}

static const char* security_conf_find_by_key(
        const struct schema_Wifi_VIF_Config *vconf,
        char *key)
{
    int     i;

    for (i = 0; i < vconf->security_len; i++)
    {
        if (!strcmp(vconf->security_keys[i], key))
        {
            return vconf->security[i];
        }
    }

    return NULL;
}

static int set_security_key_value(
        struct schema_Wifi_VIF_State *vstate,
        int index,
        const char *key,
        const char *value)
{
    STRSCPY(vstate->security_keys[index], key);
    STRSCPY(vstate->security[index], value);

    index += 1;
    vstate->security_len = index;

    return index;
}

static bool set_personal_credentials(
        struct schema_Wifi_VIF_State *vstate,
        int index,
        INT ssid_index)
{
    INT ret;
    CHAR buf[WIFIHAL_MAX_BUFFER];

    memset(buf, 0, sizeof(buf));
    ret = wifi_getApSecurityKeyPassphrase(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to retrieve security passphrase", vstate->if_name);
        return false;
    }

    if (strlen(buf) == 0)
    {
        LOGW("%s: wifi_getApSecurityKeyPassphrase returned empty SSID string", vstate->if_name);
    }

    set_security_key_value(vstate, index, OVSDB_SECURITY_KEY, buf);

    // TODO: We temporary removed multi-psk support from generic RDK platform.
    // The multi-psk support will be added back when proper
    // Wifi HAL API is created. We don't support direct hostapd-based
    // multi-psk (it should be handled within the vendor layer).

    return true;
}

static bool set_enterprise_credentials(
        struct schema_Wifi_VIF_State *vstate,
        int index,
        INT ssid_index)
{
    INT ret;
    CHAR radius_ip[WIFIHAL_MAX_BUFFER];
    CHAR radius_secret[WIFIHAL_MAX_BUFFER];
    CHAR radius_port_str[WIFIHAL_MAX_BUFFER];
    UINT radius_port;

    memset(radius_ip, 0, sizeof(radius_ip));
    memset(radius_secret, 0, sizeof(radius_secret));
    ret = wifi_getApSecurityRadiusServer(ssid_index, radius_ip, &radius_port, radius_secret);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to retrieve radius settings", vstate->if_name);
        return false;
    }

    index = set_security_key_value(vstate, index, OVSDB_SECURITY_RADIUS_SERVER_IP, radius_ip);

    memset(radius_port_str, 0, sizeof(radius_port_str));
    snprintf(radius_port_str, sizeof(radius_port_str), "%u", radius_port);
    index = set_security_key_value(vstate, index, OVSDB_SECURITY_RADIUS_SERVER_PORT, radius_port_str);

    set_security_key_value(vstate, index, OVSDB_SECURITY_RADIUS_SERVER_SECRET, radius_secret);

    return true;
}

#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
static bool vif_security_set_multi_psk_keys_from_conf(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    wifi_key_multi_psk_t *keys = NULL;
    wifi_key_multi_psk_t *keys_it = NULL;
    int keys_number = 0;
    INT ret = 0;
    int i = 0;

    for (i = 0; i < vconf->security_len; i++)
    {
        if (!strncmp(vconf->security_keys[i], "key-", 4))
        {
               keys_number++;
        }
    }

    keys = calloc(keys_number, sizeof(*keys));
    if (keys == NULL)
    {
        LOGE("vif_security_set_multi_psk_keys_from_conf: Failed to allocate memory");
        return false;
    }

    keys_it = keys;
    for (i = 0; i < vconf->security_len; i++)
    {
       if (!strncmp(vconf->security_keys[i], "key-", 4))
       {
           STRSCPY(keys_it->wifi_keyId, vconf->security_keys[i]);
           STRSCPY(keys_it->wifi_psk, vconf->security[i]);
           // MAC set to 00:00:00:00:00:00
           keys_it++;
       }

    }

    ret = wifi_pushMultiPskKeys(ssid_index, keys, keys_number);

    free(keys);
    return ret == RETURN_OK;
}

static int vif_get_security_mult_psk_keys_state(INT ssid_index, struct schema_Wifi_VIF_State *vstate, int index)
{
    int ret = 0;
    int i = 0;
    wifi_key_multi_psk_t keys[MAX_MULTI_PSK_KEYS];

    memset(keys, 0, sizeof(keys));

    ret = wifi_getMultiPskKeys(ssid_index, keys, MAX_MULTI_PSK_KEYS);
    if (ret != RETURN_OK) return index;

    for (i = 0; i < MAX_MULTI_PSK_KEYS; i++)
    {
         if (strlen(keys[i].wifi_keyId) && strlen(keys[i].wifi_psk))
         {
             index = set_security_key_value(vstate, index, keys[i].wifi_keyId, keys[i].wifi_psk);
         }
    }

    return index;
}

static bool vif_security_oftag_write(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    char fname[PATH_MAX];
    char buf[4096];
    char *pos = buf;
    size_t len = sizeof(buf);
    int i = 0;

    memset(buf, 0, len);
    snprintf(fname, sizeof(fname), "/tmp/plume/openflow%d.tags", ssid_index);

    for (i = 0; i < vconf->security_len; i++)
    {
       if (!strncmp(vconf->security_keys[i], "oftag",5))
       {
           csnprintf(&pos, &len, "%s=%s\n", vconf->security_keys[i], vconf->security[i]);
       }
    }

    return !(WARN_ON(file_put((const char *)fname, (const char *)buf) < 0));
}

static int vif_security_oftag_read(INT ssid_index, struct schema_Wifi_VIF_State *vstate, int index)
{
    char fname[PATH_MAX];
    char *psks;
    char *line;
    const char *key;
    const char *oftag;

    snprintf(fname, sizeof(fname), "/tmp/plume/openflow%d.tags", ssid_index);
    psks = file_geta(fname);
    if (!psks) return index;

    while ((line = strsep(&psks, "\t\r\n")))
    {
        if ((key = strsep(&line, "=")) && (oftag = strsep(&line, "")))
        {
            index = set_security_key_value(vstate, index, key, oftag);
        }
    }

    return index;
}
#endif

static bool set_enc_mode(
        struct schema_Wifi_VIF_State *vstate,
        INT ssid_index,
        const char *encryption,
        const char *mode,
        bool enterprise)
{
    int index = 0;

    index = set_security_key_value(vstate, index, OVSDB_SECURITY_ENCRYPTION, encryption);

    index = set_security_key_value(vstate, index, OVSDB_SECURITY_MODE, mode);

#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    index = vif_get_security_mult_psk_keys_state(ssid_index, vstate, index);

    index = vif_security_oftag_read(ssid_index, vstate, index);
#endif

    if (enterprise)
    {
        return set_enterprise_credentials(vstate, index, ssid_index);
    }

    return set_personal_credentials(vstate, index, ssid_index);
}

static bool security_to_state(
        INT ssid_index,
        char *ssid_ifname,
        struct schema_Wifi_VIF_State *vstate)
{
    sec_type_t              stype;
    c_item_t                *citem;
    CHAR                    buf[WIFIHAL_MAX_BUFFER];
    INT                     ret;

    memset(buf, 0, sizeof(buf));
    ret = wifi_getApSecurityModeEnabled(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to get security mode", ssid_ifname);
        return false;
    }

    if (!(citem = c_get_item_by_str(map_security, buf)))
    {
        LOGE("%s: Failed to decode security mode (%s)", ssid_ifname, buf);
        return false;
    }
    stype = (sec_type_t)citem->key;

    switch (stype)
    {
        case SEC_NONE:
            set_security_key_value(vstate, 0, OVSDB_SECURITY_ENCRYPTION, OVSDB_SECURITY_ENCRYPTION_OPEN);
            return true;

        case SEC_WEP_64:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WEP, OVSDB_SECURITY_MODE_WEP64, false);

        case SEC_WEP_128:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WEP, OVSDB_SECURITY_MODE_WEP128, false);

        case SEC_WPA_PERSONAL:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_PSK, OVSDB_SECURITY_MODE_WPA1, false);

        case SEC_WPA2_PERSONAL:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_PSK, OVSDB_SECURITY_MODE_WPA2, false);

        case SEC_WPA_WPA2_PERSONAL:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_PSK, OVSDB_SECURITY_MODE_MIXED, false);

        case SEC_WPA_ENTERPRISE:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_EAP, OVSDB_SECURITY_MODE_WPA1, true);

        case SEC_WPA2_ENTERPRISE:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_EAP, OVSDB_SECURITY_MODE_WPA2, true);

        case SEC_WPA_WPA2_ENTERPRISE:
            return set_enc_mode(vstate, ssid_index, OVSDB_SECURITY_ENCRYPTION_WPA_EAP, OVSDB_SECURITY_MODE_MIXED, true);

        default:
            LOGE("%s: Unsupported security type (%d = %s)", ssid_ifname, stype, buf);
            return false;
    }

    return true;
}

static bool security_to_syncmsg(
        const struct schema_Wifi_VIF_Config *vconf,
        MeshWifiAPSecurity *dest)
{
    const char  *val;
    char        *sec_str;
    int         sec_type;
    int         i;

    val = security_conf_find_by_key(vconf, OVSDB_SECURITY_ENCRYPTION);
    if (val == NULL)
    {
        LOGW("%s: Security-to-MSGQ failed -- No encryption type", vconf->if_name);
        LOGT("%s: Dumping %d security elements:", vconf->if_name, vconf->security_len);
        for (i = 0; i < vconf->security_len; i++)
        {
            LOGT("%s: ... \"%s\" = \"%s\"",
                 vconf->if_name,
                 vconf->security_keys[i],
                 vconf->security[i]);
        }
        return false;
    }

    if (strcmp(val, OVSDB_SECURITY_ENCRYPTION_WPA_PSK))
    {
        LOGW("%s: Security-to-MSGQ failed -- Encryption '%s' not supported",
             vconf->if_name, val);
        return false;
    }

    val = security_conf_find_by_key(vconf, OVSDB_SECURITY_MODE);
    if (val == NULL)
    {
        LOGW("%s: Security-to-MSGQ failed -- No mode found", vconf->if_name);
        return false;
    }

    if (!strcmp(val, OVSDB_SECURITY_MODE_WPA1))
    {
        sec_type = SEC_WPA_PERSONAL;
    }
    else if (!strcmp(val, OVSDB_SECURITY_MODE_WPA2))
    {
        sec_type = SEC_WPA2_PERSONAL;
    }
    else if (!strcmp(val, OVSDB_SECURITY_MODE_MIXED))
    {
        sec_type = SEC_WPA_WPA2_PERSONAL;
    }
    else
    {
        LOGW("%s: Security-to-MSGQ failed -- Mode '%s' unsupported",
             vconf->if_name, val);
        return false;
    }

    if (!(sec_str = c_get_str_by_key(map_security, sec_type)))
    {
        LOGW("%s: Security-to-MSGQ failed -- Sec type '%d' not found",
             vconf->if_name, sec_type);
        return false;
    }

    val = security_conf_find_by_key(vconf, OVSDB_SECURITY_KEY);
    if (val == NULL)
    {
        LOGW("%s: Security-to-MSGQ failed -- No key found", vconf->if_name);
        return false;
    }

    STRSCPY(dest->passphrase, val);
    STRSCPY(dest->secMode, sec_str);
    STRSCPY(dest->encryptMode, DEFAULT_ENC_MODE);

    return true;
}

static bool vif_is_enabled(INT ssid_index)
{
    BOOL        enabled = false;
    INT         ret;

    ret = wifi_getSSIDEnable(ssid_index, &enabled);
    if (ret != RETURN_OK)
    {
        LOGW("failed to get SSIDEnable for index %d, assuming false", ssid_index);
        enabled = false;
    }

    return enabled;
}

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

    if (!target_unmap_ifname_exists(ssid_ifname))
    {
        LOGD("%s in not in map - ignoring", ssid_ifname);
        return true;
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

bool vif_external_security_update(
        int ssid_index,
        const char *passphrase,
        const char *secMode)
{
    INT ret;
    int radio_idx;
    char radio_ifname[128];
    char ssid_ifname[128];
    struct schema_Wifi_VIF_Config vconf;
    sec_type_t stype;
    c_item_t *citem;
    const char *enc;
    const char *mode;

    memset(&vconf, 0, sizeof(vconf));
    vconf._partial_update = true;

    memset(ssid_ifname, 0, sizeof(ssid_ifname));
    ret = wifi_getApName(ssid_index, ssid_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get ap name for index %d", __func__, ssid_index);
        return false;
    }

    if (!target_unmap_ifname_exists(ssid_ifname))
    {
        LOGD("%s in not in map - ignoring", ssid_ifname);
        return true;
    }

    ret = wifi_getSSIDRadioIndex(ssid_index, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio idx for SSID %s", __func__, ssid_ifname);
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

    if (!(citem = c_get_item_by_str(map_security, secMode)))
    {
        LOGE("%s: Failed to decode security mode (%s)", ssid_ifname, secMode);
        return false;
    }
    stype = (sec_type_t)citem->key;

    switch (stype)
    {
        case SEC_NONE:
            enc = OVSDB_SECURITY_ENCRYPTION;
            mode = OVSDB_SECURITY_ENCRYPTION_OPEN;
            break;

        case SEC_WEP_64:
            enc = OVSDB_SECURITY_ENCRYPTION_WEP;
            mode = OVSDB_SECURITY_MODE_WEP64;
            break;

        case SEC_WEP_128:
            enc = OVSDB_SECURITY_ENCRYPTION_WEP;
            mode = OVSDB_SECURITY_MODE_WEP128;
            break;

        case SEC_WPA_PERSONAL:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_PSK;
            mode = OVSDB_SECURITY_MODE_WPA1;
            break;

        case SEC_WPA2_PERSONAL:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_PSK;
            mode = OVSDB_SECURITY_MODE_WPA2;
            break;

        case SEC_WPA_WPA2_PERSONAL:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_PSK;
            mode = OVSDB_SECURITY_MODE_MIXED;
            break;

        case SEC_WPA_ENTERPRISE:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_EAP;
            mode = OVSDB_SECURITY_MODE_WPA1;
            break;

        case SEC_WPA2_ENTERPRISE:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_EAP;
            mode = OVSDB_SECURITY_MODE_WPA2;
            break;

        case SEC_WPA_WPA2_ENTERPRISE:
            enc = OVSDB_SECURITY_ENCRYPTION_WPA_EAP;
            mode = OVSDB_SECURITY_MODE_MIXED;
            break;

        default:
            LOGE("%s: Unsupported security type (%d = %s)", ssid_ifname, stype, secMode);
            return false;
    }

    STRSCPY(vconf.security_keys[0], OVSDB_SECURITY_ENCRYPTION);
    STRSCPY(vconf.security[0], enc);
    STRSCPY(vconf.security_keys[1], OVSDB_SECURITY_KEY);
    STRSCPY(vconf.security[1], passphrase);
    STRSCPY(vconf.security_keys[2], OVSDB_SECURITY_MODE);
    STRSCPY(vconf.security[2], mode);
    vconf.security_len = 3;
    vconf.security_present = true;

    SCHEMA_SET_STR(vconf.if_name, target_unmap_ifname(ssid_ifname));

    LOGD("Updating VIF for new security");
    radio_rops_vconfig(&vconf, radio_ifname);

    return true;
}

bool vif_copy_to_config(
        INT ssidIndex,
        struct schema_Wifi_VIF_State *vstate,
        struct schema_Wifi_VIF_Config *vconf)
{
    int i;

    memset(vconf, 0, sizeof(*vconf));
    schema_Wifi_VIF_Config_mark_all_present(vconf);
    vconf->_partial_update = true;

    SCHEMA_SET_STR(vconf->if_name, vstate->if_name);
    LOGT("vconf->ifname = %s", vconf->if_name);
    SCHEMA_SET_STR(vconf->mode, vstate->mode);
    LOGT("vconf->mode = %s", vconf->mode);
    SCHEMA_SET_INT(vconf->enabled, vstate->enabled);
    LOGT("vconf->enabled = %d", vconf->enabled);
    if (vstate->vlan_id_exists)
    {
        SCHEMA_SET_INT(vconf->vlan_id, vstate->vlan_id);
    }
    LOGT("vconf->vlan_id = %d", vconf->vlan_id);
    SCHEMA_SET_INT(vconf->ap_bridge, vstate->ap_bridge);
    LOGT("vconf->ap_bridge = %d", vconf->ap_bridge);
    SCHEMA_SET_INT(vconf->wds, vstate->wds);
    LOGT("vconf->wds = %d", vconf->wds);
    SCHEMA_SET_INT(vconf->vif_radio_idx, vstate->vif_radio_idx);
    LOGT("vconf->vif_radio_idx = %d", vconf->vif_radio_idx);
    SCHEMA_SET_STR(vconf->ssid_broadcast, vstate->ssid_broadcast);
    LOGT("vconf->ssid_broadcast = %s", vconf->ssid_broadcast);
    SCHEMA_SET_STR(vconf->ssid, vstate->ssid);
    LOGT("vconf->ssid = %s", vconf->ssid);
    SCHEMA_SET_INT(vconf->rrm, vstate->rrm);
    LOGT("vconf->rrm = %d", vconf->rrm);
    SCHEMA_SET_INT(vconf->btm, vstate->btm);
    LOGT("vconf->btm = %d", vconf->btm);
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

    // security, security_keys, security_len
    for (i = 0; i < vstate->security_len; i++)
    {
        STRSCPY(vconf->security_keys[i], vstate->security_keys[i]);
        STRSCPY(vconf->security[i],      vstate->security[i]);
    }
    vconf->security_len = vstate->security_len;

    // mac_list, mac_list_len
    SCHEMA_SET_STR(vconf->mac_list_type, vstate->mac_list_type);
    for (i = 0; i < vstate->mac_list_len; i++)
    {
        STRSCPY(vconf->mac_list[i], vstate->mac_list[i]);
    }
    vconf->mac_list_len = vstate->mac_list_len;

    return true;
}

static bool vif_get_radio_ifname(
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

bool vif_state_get(
        INT ssidIndex,
        struct schema_Wifi_VIF_State *vstate)
{
    ULONG                           lval;
    CHAR                            buf[WIFIHAL_MAX_BUFFER];
    BOOL                            bval;
    char                            *str;
    INT                             ret;
    INT                             radio_idx;
    char                            ssid_ifname[128];
    BOOL                            rrm;
    BOOL                            btm;

    memset(vstate, 0, sizeof(*vstate));
    schema_Wifi_VIF_State_mark_all_present(vstate);
    vstate->_partial_update = true;
    vstate->associated_clients_present = false;
    vstate->vif_config_present = false;

    memset(ssid_ifname, 0, sizeof(ssid_ifname));
    ret = wifi_getApName(ssidIndex, ssid_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get ap name for index %d", __func__, ssidIndex);
        return false;
    }

    SCHEMA_SET_STR(vstate->if_name, target_unmap_ifname(ssid_ifname));

    // mode (w/ exists)
    SCHEMA_SET_STR(vstate->mode, "ap");

    // enabled (w/ exists)
    SCHEMA_SET_INT(vstate->enabled, vif_is_enabled(ssidIndex));

    // vlan_id (w/ exists)
    SCHEMA_SET_INT(vstate->vlan_id, target_map_ifname_to_vlan(vstate->if_name));
    if (vstate->vlan_id == 0)
    {
        vstate->vlan_id_exists = false;
    }

    // wds (w/ exists)
    SCHEMA_SET_INT(vstate->wds, false);

    // ap_bridge (w/ exists)
    ret = wifi_getApIsolationEnable(ssidIndex, &bval);
    if (ret == RETURN_OK)
    {
        SCHEMA_SET_INT(vstate->ap_bridge, bval ? false : true);
    } else
    {
        LOGW("Cannot getApIsolationEnable for SSID index %d", ssidIndex);
    }

    // vif_radio_idx (w/ exists)
    SCHEMA_SET_INT(vstate->vif_radio_idx, target_map_ifname_to_vif_radio_idx(vstate->if_name));

    // ssid_broadcast (w/ exists)
    ret = wifi_getApSsidAdvertisementEnable(ssidIndex, &bval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get SSID Advertisement Enable", ssid_ifname);
    }
    else
    {
        str = c_get_str_by_key(map_enable_disable, bval);
        if (strlen(str) == 0)
        {
            LOGW("%s: failed to decode ssid_enable (%d)", ssid_ifname, bval);
        }
        else
        {
            SCHEMA_SET_STR(vstate->ssid_broadcast, str);
        }
    }

    // SSID (w/ exists)
    memset(buf, 0, sizeof(buf));
    ret = wifi_getSSIDName(ssidIndex, buf);
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get SSID", ssid_ifname);
    }
    else
    {
        SCHEMA_SET_STR(vstate->ssid, buf);
    }

    // security, security_keys, security_len
    if (!security_to_state(ssidIndex, ssid_ifname, vstate))
    {
        LOGW("%s: cannot get security for %s", __func__, ssid_ifname);
    }

#ifdef CONFIG_RDK_WPS_SUPPORT
    wps_to_state(ssidIndex, vstate);
#endif

    // mac_list_type (w/ exists)
    // mac_list, mac_list_len
    if (!acl_to_state(ssidIndex, ssid_ifname, vstate))
    {
        LOGW("%s: cannot get ACL for %s", __func__, ssid_ifname);
    }

    ret = wifi_getSSIDRadioIndex(ssidIndex, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio idx for SSID %s\n", __func__, ssid_ifname);
        return false;
    }

    // SSID (w/ exists)
    memset(buf, 0, sizeof(buf));
    ret = wifi_getSSIDNameStatus(ssidIndex, buf);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get active SSID", ssid_ifname);
    }
    else
    {
        SCHEMA_SET_STR(vstate->ssid, buf);
    }

    // mac (w/ exists)
    memset(buf, 0, sizeof(buf));
    ret = wifi_getBaseBSSID(ssidIndex, buf);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get base BSSID (mac)", ssid_ifname);
    }
    else
    {
        SCHEMA_SET_STR(vstate->mac, buf);
    }

    // channel (w/ exists)
    ret = wifi_getRadioChannel(radio_idx, &lval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get channel from radio idx %d", ssid_ifname, radio_idx);
    }
    else
    {
        SCHEMA_SET_INT(vstate->channel, lval);
    }

    ret = wifi_getNeighborReportActivation(ssidIndex, &rrm);
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get RRM", ssid_ifname);
    }
    else
    {
        SCHEMA_SET_INT(vstate->rrm, rrm);
    }

    ret = wifi_getBSSTransitionActivation(ssidIndex, &btm);
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get BTM", ssid_ifname);
    }
    else
    {
        SCHEMA_SET_INT(vstate->btm, btm);
    }

    return true;
}

static bool vif_ifname_to_idx(const char *ifname, INT *outSsidIndex)
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
    BOOL bval;
    INT ret;
    c_item_t *citem;
    char tmp[256];
    MeshWifiAPSecurity sec;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    if (!vif_ifname_to_idx(ssid_ifname, &ssid_index))
    {
        LOGE("%s: cannot get index for %s", __func__, ssid_ifname);
        return false;
    }

    acl_apply(ssid_index, ssid_ifname, vconf);

    if (changed->ssid_broadcast)
    {
        if ((citem = c_get_item_by_str(map_enable_disable, vconf->ssid_broadcast)))
        {
            bval = citem->key ? TRUE : FALSE;
            ret = wifi_setApSsidAdvertisementEnable(ssid_index, bval);
            LOGD("[WIFI_HAL SET] wifi_setApSsidAdvertisementEnable(%d, %d) = %d",
                                                    ssid_index, bval, ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to set SSID Broadcast to '%d'", ssid_ifname, bval);
            }
            else
            {
                if (!sync_send_ssid_broadcast_change(ssid_index, bval))
                {
                    LOGW("%s: Failed to sync SSID Broadcast change to %s",
                        ssid_ifname, (bval ? "true" : "false"));
                }
            }

            LOGI("%s: Updated SSID Broadcast to %d", ssid_ifname, bval);
        }
        else
        {
            LOGW("%s: Failed to decode ssid_broadcast \"%s\"",
                 ssid_ifname, vconf->ssid_broadcast);
        }
    }

    if (changed->vlan_id)
    {
        if (!target_map_update_vlan(vconf->if_name, vconf->vlan_id))
        {
            LOGW("%s: Failed to update VLAN to %u", ssid_ifname, vconf->vlan_id);
        }
        else
        {
            LOGI("%s: Updated VLAN to %u", ssid_ifname, vconf->vlan_id);
        }
    }

    if (strlen(vconf->ssid) == 0)
    {
        LOGW("%s: vconf->ssid string is empty", ssid_ifname);
    }
    else if (changed->ssid)
    {
        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, sizeof(tmp) - 1, "%s", vconf->ssid);
        ret = wifi_setSSIDName(ssid_index, tmp);
        LOGD("[WIFI_HAL SET] wifi_setSSIDName(%d, \"%s\") = %d",
                               ssid_index, tmp, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to set new SSID '%s'", ssid_ifname, tmp);
        }
        else
        {
            LOGI("%s: SSID updated to '%s'", ssid_ifname, tmp);
            if (!sync_send_ssid_change(ssid_index, ssid_ifname, vconf->ssid))
            {
                LOGE("%s: Failed to sync SSID change to '%s'", ssid_ifname, vconf->ssid);
            }
        }
    }

#ifdef CONFIG_RDK_WPS_SUPPORT
    vif_config_set_wps(ssid_index, vconf, changed, rconf->if_name);
#endif

    if (changed->security && vconf->security_len)
    {
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
        if (!vif_security_set_multi_psk_keys_from_conf(ssid_index, vconf))
        {
            LOGW("%s: Failed to set multi-psk config", ssid_ifname);
        }

        if (!vif_security_oftag_write(ssid_index, vconf))
        {
            LOGW("%s: Failed to save oftags", ssid_ifname);
        }
#endif
        memset(&sec, 0, sizeof(sec));
        if (!security_to_syncmsg(vconf, &sec))
        {
            LOGW("%s: Failed to convert security for sync", ssid_ifname);
        }
        else
        {
            sec.index = ssid_index;

            ret = wifi_setApSecurityModeEnabled(sec.index, sec.secMode);
            LOGD("[WIFI_HAL SET] wifi_setApSecurityModeEnabled(%d, \"%s\") = %d",
                 sec.index, sec.secMode, ret);
            if (ret != RETURN_OK)
            {
                LOGW("%s: Failed to set new security mode to '%s'",
                     ssid_ifname, sec.secMode);
            }
            else
            {
                if (strlen(sec.passphrase) == 0)
                {
                    LOGW("%s: security_to_syncmsg returned empty sec.passphrase", ssid_ifname);
                }
                else
                {
                    ret = wifi_setApSecurityKeyPassphrase(sec.index, sec.passphrase);
                    LOGD("[WIFI_HAL SET] wifi_setApSecurityKeyPassphrase(%d, \"%s\") = %d",
                         sec.index, sec.passphrase, ret);
                    if (ret != RETURN_OK)
                    {
                        LOGW("%s: Failed to set new security passphrase", ssid_ifname);
                    }
                }
                LOGI("%s: Security settings updated", ssid_ifname);

                if (!sync_send_security_change(ssid_index, ssid_ifname, &sec))
                {
                    LOGW("%s: Failed to sync security change", ssid_ifname);
                }
            }
        }
    }


    if (changed->enabled)
    {
        ret = wifi_setSSIDEnable(ssid_index, vconf->enabled);
        LOGD("[WIFI_HAL SET] wifi_setSSIDEnable(%d, %d) = %d",
                                 ssid_index, vconf->enabled, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to change enable to %d", ssid_ifname, vconf->enabled);
        }
    }

    if (changed->ap_bridge)
    {
        ret = wifi_setApIsolationEnable(ssid_index, vconf->ap_bridge ? false : true);
        LOGD("[WIFI_HAL SET] wifi_setApIsolationEnable(%d, %d) = %d",
                                        ssid_index, !vconf->ap_bridge, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to change ap_bridge to %d", ssid_ifname, vconf->ap_bridge);
        }
    }

    if (changed->rrm)
    {
        if (wifi_setNeighborReportActivation(ssid_index, vconf->rrm) != RETURN_OK)
        {
            LOGW("%s: Failed to change rrm to %d", ssid_ifname, vconf->rrm);
        }
    }

    if (changed->btm)
    {
        if (wifi_setBSSTransitionActivation(ssid_index, vconf->btm) != RETURN_OK)
        {
            LOGW("%s: Failed to change btm to %d", ssid_ifname, vconf->btm);
        }
    }

    if (wifi_applySSIDSettings(ssid_index) != RETURN_OK)
    {
        LOGW("%s: Failed to apply SSID settings", ssid_ifname);
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
