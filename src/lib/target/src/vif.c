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

static char vif_bridge_name[128];

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

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
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
#endif

#define OVSDB_SECURITY_KEY_MGMT_DPP        "dpp"
#define OVSDB_SECURITY_KEY_MGMT_WPA_PSK    "wpa-psk"
#define OVSDB_SECURITY_KEY_MGMT_WPA2_PSK   "wpa2-psk"
#define OVSDB_SECURITY_KEY_MGMT_WPA2_EAP   "wpa2-eap"
#define OVSDB_SECURITY_KEY_MGMT_SAE        "sae"
#define RDK_SECURITY_KEY_MGMT_OPEN         "None"
#define RDK_SECURITY_KEY_MGMT_WPA_PSK      "WPA-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA2_PSK     "WPA2-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA_WPA2_PSK "WPA-WPA2-Personal"
#define RDK_SECURITY_KEY_MGMT_WPA2_EAP     "WPA2-Enterprise"
#define RDK_SECURITY_KEY_MGMT_WPA3         "WPA3-Sae"

static bool acl_to_state(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    char                    acl_buf[ACL_BUF_SIZE];
    char                    *p;
    char                    *s = NULL;
    INT                     acl_mode;
    INT                     status = RETURN_ERR;
    INT                     i;
    char                    *ssid_ifname = vstate->if_name;

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
        const struct schema_Wifi_VIF_Config *vconf)
{
    c_item_t                *citem;
    INT                     acl_mode;
    INT                     ret;
    INT                     i;
    const char              *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    // !!! XXX: Cannot touch ACL for home interfaces, since they are currently
    //          used for band steering.
    if (!strcmp(ssid_ifname, CONFIG_RDK_HOME_AP_24_IFNAME) ||
        !strcmp(ssid_ifname, CONFIG_RDK_HOME_AP_50_IFNAME))
    {
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

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
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
#endif

static bool security_key_mgmt_hal_to_ovsdb(
       const char *key_mgmt,
       struct schema_Wifi_VIF_State *vstate,
       struct schema_Wifi_VIF_Config *vconfig
        )
{
    if (!strcmp(key_mgmt, RDK_SECURITY_KEY_MGMT_WPA_PSK))
    {
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
        return true;
    }
    if (!strcmp(key_mgmt, RDK_SECURITY_KEY_MGMT_WPA2_PSK))
    {
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
        return true;
    }
    if (!strcmp(key_mgmt, RDK_SECURITY_KEY_MGMT_WPA_WPA2_PSK))
    {
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA_PSK);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_PSK);
        return true;
    }
    if (!strcmp(key_mgmt, RDK_SECURITY_KEY_MGMT_WPA3))
    {
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_SAE);
        return true;
    }
    // The only 'enterpise' encryption present in OVSDB schema is WPA2-Enterpise, so skip
    // other RDK 'enterpise' types.
    if (!strcmp(key_mgmt, RDK_SECURITY_KEY_MGMT_WPA2_EAP))
    {
        if (vstate) SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP);
        if (vconfig) SCHEMA_VAL_APPEND(vconfig->wpa_key_mgmt, OVSDB_SECURITY_KEY_MGMT_WPA2_EAP);
        return true;
    }

    LOGW("%s: unsupported security key mgmt %s", __func__, key_mgmt);
    return false;
}

static bool get_enterprise_credentials(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    CHAR radius_ip[WIFIHAL_MAX_BUFFER];
    CHAR radius_secret[WIFIHAL_MAX_BUFFER];
    UINT radius_port;

    memset(radius_ip, 0, sizeof(radius_ip));
    memset(radius_secret, 0, sizeof(radius_secret));
    LOGT("wifi_getApSecurityRadiusServer() index=%d", ssid_index);
    ret = wifi_getApSecurityRadiusServer(ssid_index, radius_ip, &radius_port, radius_secret);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getApSecurityRadiusServer() FAILED index=%d ret=%d", ssid_index, ret);
        return false;
    }
    LOGT("wifi_getApSecurityRadiusServer() OK index=%d ip='%s' port=%d", ssid_index,
          radius_ip, radius_port);

    SCHEMA_SET_STR(vstate->radius_srv_addr, radius_ip);
    SCHEMA_SET_INT(vstate->radius_srv_port, radius_port);
    SCHEMA_SET_STR(vstate->radius_srv_secret, radius_secret);

    return true;
}

static bool get_psks(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    CHAR buf[WIFIHAL_MAX_BUFFER];
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_key_multi_psk_t keys[MAX_MULTI_PSK_KEYS];
    int i;
#endif

    memset(buf, 0, sizeof(buf));
    LOGT("wifi_getApSecurityKeyPassphrase() index=%d", ssid_index);
    ret = wifi_getApSecurityKeyPassphrase(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getApSecurityKeyPassphrase() FAILED index=%d", ssid_index);
        return false;
    }
    LOGT("wifi_getApSecurityKeyPassphrase() OK index=%d", ssid_index);

    if (strlen(buf) == 0)
    {
        LOGW("wifi_getApSecurityKeyPassphrase() returned an empty SSID string, index=%d",
              ssid_index);
    }

    SCHEMA_KEY_VAL_APPEND(vstate->wpa_psks, "key", buf);

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
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    CHAR buf[WIFIHAL_MAX_BUFFER];
    INT ret;

    memset(buf, 0, sizeof(buf));
    LOGT("wifi_getApSecurityModeEnabled() index=%d", ssid_index);
    ret = wifi_getApSecurityModeEnabled(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getApSecurityModeEnabled() index=%d, ret=%d", ssid_index, ret);
        return false;
    }
    LOGT("wifi_getApSecurityModeEnabled() OK index=%d mode='%s'", ssid_index, buf);

    if (!strcmp(buf, RDK_SECURITY_KEY_MGMT_OPEN))
    {
        SCHEMA_SET_INT(vstate->wpa, 0);
        return true;
    }

    SCHEMA_SET_INT(vstate->wpa, 1);
    if (!security_key_mgmt_hal_to_ovsdb(buf, vstate, NULL)) return false;

    // The only 'enterprise' encryption present in OVSDB schema is WPA2-Enterpise, so skip
    // other RDK 'enterprise' types.
    if (!strcmp(buf, RDK_SECURITY_KEY_MGMT_WPA2_EAP))
    {
        return get_enterprise_credentials(ssid_index, vstate);
    }

    return get_psks(ssid_index, vstate);
}

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
static bool security_to_state(
        INT ssid_index,
        struct schema_Wifi_VIF_State *vstate)
{
    sec_type_t              stype;
    c_item_t                *citem;
    CHAR                    buf[WIFIHAL_MAX_BUFFER];
    INT                     ret;
    char                    *ssid_ifname = vstate->if_name;

    memset(buf, 0, sizeof(buf));
    LOGT("Legacy wifi_getApSecurityModeEnabled(%d)", ssid_index);
    ret = wifi_getApSecurityModeEnabled(ssid_index, buf);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to get security mode", ssid_ifname);
        return false;
    }
    LOGT("Legacy wifi_getApSecurityModeEnabled(%d) OK", ssid_index);

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

static const char *security_key_mgmt_ovsdb_to_hal(const struct schema_Wifi_VIF_Config *vconf)
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
    const char *mode = security_key_mgmt_ovsdb_to_hal(vconf);

    if (!mode) return false;
    STRSCPY(dest->secMode, mode);

    STRSCPY(dest->passphrase, vconf->wpa_psks[0]); // MeshAgent doesn't support Multi-PSK
    STRSCPY(dest->encryptMode, DEFAULT_ENC_MODE);

    dest->index = ssid_index;

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
#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
    sec_type_t stype;
    c_item_t *citem;
    const char *enc;
    const char *mode;
#endif

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

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
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
#endif

    if (!strcmp(secMode, RDK_SECURITY_KEY_MGMT_OPEN))
    {
        SCHEMA_SET_INT(vconf.wpa, 0);
    }
    else
    {
         if (!security_key_mgmt_hal_to_ovsdb(secMode, NULL, &vconf)) return false;

         SCHEMA_SET_INT(vconf.wpa, 1);
         SCHEMA_KEY_VAL_APPEND(vconf.wpa_psks, "key", passphrase);
    }

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

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
    LOGT("Copying legacy security settings");
    // security, security_keys, security_len
    for (i = 0; i < vstate->security_len; i++)
    {
        STRSCPY(vconf->security_keys[i], vstate->security_keys[i]);
        STRSCPY(vconf->security[i],      vstate->security[i]);
    }
    vconf->security_len = vstate->security_len;
#endif

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

    // mac_list, mac_list_len
    SCHEMA_SET_STR(vconf->mac_list_type, vstate->mac_list_type);
    for (i = 0; i < vstate->mac_list_len; i++)
    {
        STRSCPY(vconf->mac_list[i], vstate->mac_list[i]);
    }
    vconf->mac_list_len = vstate->mac_list_len;

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

static bool get_if_name(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    char ssid_ifname[128];

    memset(ssid_ifname, 0, sizeof(ssid_ifname));
    LOGT("wifi_getApName() index=%d", ssidIndex);
    ret = wifi_getApName(ssidIndex, ssid_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getApName() FAILED index=%d ret=%d", ssidIndex, ret);
        return false;
    }
    LOGT("wifi_getApName() OK index=%d if_name='%s'", ssidIndex, ssid_ifname);

    SCHEMA_SET_STR(vstate->if_name, target_unmap_ifname(ssid_ifname));
    return true;
}

static void get_ap_bridge(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    BOOL bval = false;

    LOGT("wifi_getApIsolationEnable() index=%d", ssidIndex);
    ret = wifi_getApIsolationEnable(ssidIndex, &bval);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getApIsolationEnable() FAILED index=%d ret=%d", ssidIndex, ret);
        return;
    }

    LOGT("wifi_getApIsolationEnable() OK index=%d bval=%d", ssidIndex, bval);
    SCHEMA_SET_INT(vstate->ap_bridge, bval ? false : true);
}

static void get_ssid_broadcast(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    BOOL bval = false;
    char *str = NULL;

    LOGT("wifi_getApSsidAdvertisementEnable() index=%d", ssidIndex);
    ret = wifi_getApSsidAdvertisementEnable(ssidIndex, &bval);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getApSsidAdvertisementEnable() FAILED index=%d ret=%d", ssidIndex, ret);
    }
    LOGT("wifi_getApSsidAdvertisementEnable() OK index=%d bval=%d", ssidIndex, bval);

    str = c_get_str_by_key(map_enable_disable, bval);
    if (strlen(str) == 0)
    {
        LOGW("Failed to decode ssid_enable index=%d bval=%d", ssidIndex, bval);
        return;
    }

    SCHEMA_SET_STR(vstate->ssid_broadcast, str);
}

static void get_ssid(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    CHAR buf[WIFIHAL_MAX_BUFFER];

    memset(buf, 0, sizeof(buf));

    if (vstate->enabled)
    {
        LOGT("wifi_getSSIDNameStatus() index=%d", ssidIndex);
        ret = wifi_getSSIDNameStatus(ssidIndex, buf);
        if (ret != RETURN_OK)
        {
            LOGW("wifi_getSSIDNameStatus() FAILED index=%d ret=%d", ssidIndex, ret);
            return;
        }
        LOGT("wifi_getSSIDNameStatus() OK index=%d buf='%s'", ssidIndex, buf);
    }
    else // If ssid is disabled read SSID name from config
    {
        LOGT("wifi_getSSIDName() index=%d", ssidIndex);
        ret = wifi_getSSIDName(ssidIndex, buf);
        if (ret != RETURN_OK)
        {
            LOGW("wifi_getSSIDName() FAILED index=%d ret=%d", ssidIndex, ret);
            return;
        }
        LOGT("wifi_getSSIDName() OK index=%d buf='%s'", ssidIndex, buf);
    }

    SCHEMA_SET_STR(vstate->ssid, buf);
}

static bool get_channel(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    INT radio_idx = -1;
    ULONG channel = 0;

    LOGT("wifi_getSSIDRadioIndex() index=%d", ssidIndex);
    ret = wifi_getSSIDRadioIndex(ssidIndex, &radio_idx);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_getSSIDRadioIndex() FAILED index=%d", ssidIndex);
        return false;
    }
    LOGT("wifi_getSSIDRadioIndex() OK index=%d radio_idx=%d", ssidIndex, radio_idx);

    LOGT("wifi_getRadioChannel() radio_index=%d", radio_idx);
    ret = wifi_getRadioChannel(radio_idx, &channel);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getRadioChannel() FAILED radio_idx=%d ret=%d", radio_idx, ret);
        return true;
    }
    LOGT("wifi_getRadioChannel() OK radio_idx=%d channel=%lu", radio_idx, channel);

    SCHEMA_SET_INT(vstate->channel, channel);
    return true;
}

static void get_mac(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    CHAR mac_str[WIFIHAL_MAX_BUFFER];

    memset(mac_str, 0, sizeof(mac_str));
    LOGT("wifi_getBaseBSSID() index=%d", ssidIndex);
    ret = wifi_getBaseBSSID(ssidIndex, mac_str);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getBaseBSSID() FAILED index=%d ret=%d", ssidIndex, ret);
        return;
    }
    LOGT("wifi_getBaseBSSID() OK index=%d mac='%s'", ssidIndex, mac_str);

    SCHEMA_SET_STR(vstate->mac, mac_str);
}

static void get_rrm(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    BOOL rrm = false;

    LOGT("wifi_getNeighborReportActivation() index=%d", ssidIndex);
    ret = wifi_getNeighborReportActivation(ssidIndex, &rrm);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getNeighborReportActivation() FAILED index=%d ret=%d", ssidIndex, ret);
        return;
    }
    LOGT("wifi_getNeighborReportActivation() OK index=%d rrm=%d", ssidIndex, rrm);

    SCHEMA_SET_INT(vstate->rrm, rrm);
}

static void get_btm(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    BOOL btm = false;

    LOGT("wifi_getBSSTransitionActivation() index=%d", ssidIndex);
    ret = wifi_getBSSTransitionActivation(ssidIndex, &btm);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getBSSTransitionActivation() FAILED index=%d ret=%d", ssidIndex, ret);
        return;
    }
    LOGT("wifi_getBSSTransitionActivation() OK index=%d btm=%d", ssidIndex, btm);

    SCHEMA_SET_INT(vstate->btm, btm);
}

static void get_uapsd_enable(INT ssidIndex, struct schema_Wifi_VIF_State *vstate)
{
    INT ret;
    BOOL uapsd_enable = false;

    LOGT("wifi_getApWmmUapsdEnable() index=%d", ssidIndex);
    ret = wifi_getApWmmUapsdEnable(ssidIndex, &uapsd_enable);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_getApWmmUapsdEnable() FAILED index=%d ret=%d", ssidIndex, ret);
        return;
    }
    LOGT("wifi_getApWmmUapsdEnable() OK index=%d uapsd_enable=%d", ssidIndex, uapsd_enable);

    SCHEMA_SET_INT(vstate->uapsd_enable, uapsd_enable);
}

static void get_bridge(struct schema_Wifi_VIF_State *vstate)
{
    if (strlen(vif_bridge_name) > 0)
    {
        SCHEMA_SET_STR(vstate->bridge, vif_bridge_name);
        LOGT("vstate->bridge set to '%s'", vstate->bridge);
    }
    else
    {
        vstate->bridge_exists = false;
    }
}

bool vif_state_get(
        INT ssidIndex,
        struct schema_Wifi_VIF_State *vstate)
{
    memset(vstate, 0, sizeof(*vstate));
    schema_Wifi_VIF_State_mark_all_present(vstate);
    vstate->_partial_update = true;
    vstate->associated_clients_present = false;
    vstate->vif_config_present = false;

    if (get_if_name(ssidIndex, vstate) != true) return false;
    if (get_channel(ssidIndex, vstate) != true) return false;

    SCHEMA_SET_INT(vstate->enabled, vif_is_enabled(ssidIndex));
    SCHEMA_SET_STR(vstate->mode, "ap");
    SCHEMA_SET_INT(vstate->wds, false);

    get_ssid(ssidIndex, vstate);
    get_ap_bridge(ssidIndex, vstate);
    get_ssid_broadcast(ssidIndex, vstate);
    get_mac(ssidIndex, vstate);
    get_rrm(ssidIndex, vstate);
    get_btm(ssidIndex, vstate);
    get_uapsd_enable(ssidIndex, vstate);
    get_bridge(vstate);

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
    security_to_state(ssidIndex, vstate);
#endif
    get_security(ssidIndex, vstate);
    acl_to_state(ssidIndex, vstate);

#ifdef CONFIG_RDK_WPS_SUPPORT
    wps_to_state(ssidIndex, vstate);
#endif

#ifdef CONFIG_RDK_MULTI_AP_SUPPORT
    multi_ap_to_state(ssidIndex, vstate);
#endif

    return true;
}

static void set_security_mode(INT ssid_index, const char *mode)
{
    INT ret;

    LOGT("wifi_setApSecurityModeEnabled() index=%d mode='%s'", ssid_index,
        mode);
    ret = wifi_setApSecurityModeEnabled(ssid_index, (char *)mode);
    if (ret != RETURN_OK)
    {
        LOGE("wifi_setApSecurityModeEnabled() FAILED index=%d mode='%s'",
            ssid_index, mode);
    }

    LOGT("wifi_setApSecurityModeEnabled() OK index=%d mode='%s'", ssid_index,
        mode);
}

static bool set_password(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        MeshWifiAPSecurity *mesh_security_data)
{
    INT ret;
    char passphrase[WIFIHAL_MAX_BUFFER];
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    wifi_key_multi_psk_t *keys = NULL;
    int i;
#endif

    memset(passphrase, 0, sizeof(passphrase));
    STRSCPY(passphrase, vconf->wpa_psks[0]); // Needed as RDK HAL discards const
    LOGT("wifi_setApSecurityKeyPassphrase() index=%d", ssid_index);
    ret = wifi_setApSecurityKeyPassphrase(ssid_index, passphrase);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_setApSecurityKeyPassphrase() FAILED index=%d", ssid_index);
        return false;
    }
    LOGT("wifi_setApSecurityKeyPassphrase() OK index=%d", ssid_index);

    STRSCPY(mesh_security_data->passphrase, passphrase);

#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    if (vconf->wpa_psks_len == 1) return true;

    keys = calloc(vconf->wpa_psks_len - 1, sizeof(wifi_key_multi_psk_t));
    if (keys == NULL)
    {
        LOGE("%s: Failed to allocate memory for multi-psk keys, index=%d", __func__,
                ssid_index);
        return false;
    }

    for (i = 1; i < vconf->wpa_psks_len; i++)
    {
       STRSCPY(keys[i - 1].wifi_keyId, vconf->wpa_psks_keys[i]);
       STRSCPY(keys[i - 1].wifi_psk, vconf->wpa_psks[i]);
       // MAC set to 00:00:00:00:00:00
    }
    LOGT("wifi_pushMultiPskKeys() index=%d", ssid_index);
    ret = wifi_pushMultiPskKeys(ssid_index, keys, vconf->wpa_psks_len - 1);
    if (ret != RETURN_OK)
    {
        LOGW("wifi_pushMultiPskKeys() FAILED index=%d", ssid_index);
    }
    LOGT("wifi_pushMultiPskKeys() OK index=%d", ssid_index);

    free(keys);
#endif

    return true;
}

static void set_security(
        INT ssid_index,
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_VIF_Config_flags *changed)
{
    const char *key_mgmt = NULL;
    MeshWifiAPSecurity mesh_security_data;
    bool send_sync = false;

    memset(&mesh_security_data, 0, sizeof(mesh_security_data));

    // Prepare sync message in case it needs to be updated and sent
    security_ovsdb_to_syncmsg(ssid_index, vconf, &mesh_security_data);

    if (changed->wpa && vconf->wpa == 0)
    {
        set_security_mode(ssid_index, RDK_SECURITY_KEY_MGMT_OPEN);
        STRSCPY(mesh_security_data.secMode, RDK_SECURITY_KEY_MGMT_OPEN);
        send_sync = true;
        goto exit;
    }

    if (changed->wpa_key_mgmt)
    {
        key_mgmt = security_key_mgmt_ovsdb_to_hal(vconf);
        if (!key_mgmt) return;
        set_security_mode(ssid_index, key_mgmt);
        send_sync = true;
    }

    if (changed->wpa_psks && vconf->wpa_psks_len >= 1)
    {
        if (!set_password(ssid_index, vconf, &mesh_security_data)) goto exit;
        send_sync = true;
    }

exit:
    if (send_sync)
    {
        if (!sync_send_security_change(ssid_index, vconf->if_name, &mesh_security_data))
        {
            LOGW("%s: Failed to sync security change", vconf->if_name);
        }
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

static void set_ssid_broadcast(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    c_item_t *citem;
    INT ret;
    BOOL enable = false;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    if ((citem = c_get_item_by_str(map_enable_disable, vconf->ssid_broadcast)))
    {
        enable = citem->key ? TRUE : FALSE;
        LOGT("wifi_setApSsidAdvertisementEnable index=%d enable=%d", ssid_index, enable);
        ret = wifi_setApSsidAdvertisementEnable(ssid_index, enable);
        LOGT("wifi_setApSsidAdvertisementEnable index=%d enable=%d ret=%d", ssid_index,
                enable, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to set SSID Broadcast to '%d'", ssid_ifname, enable);
            return;
        }
#ifndef CONFIG_RDK_DISABLE_SYNC
        if (!sync_send_ssid_broadcast_change(ssid_index, enable))
        {
            LOGW("%s: Failed to sync SSID Broadcast change to %s",
                    ssid_ifname, (enable ? "true" : "false"));
        }
#endif

        LOGI("%s: Updated SSID Broadcast to %d", ssid_ifname, enable);
        return;
    }

    LOGW("%s: Failed to decode ssid_broadcast \"%s\"",
            ssid_ifname, vconf->ssid_broadcast);
}

static void set_ssid(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    char tmp[256];
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    if (strlen(vconf->ssid) == 0)
    {
        LOGW("%s: vconf->ssid string is empty", ssid_ifname);
        return;
    }

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp) - 1, "%s", vconf->ssid);
    LOGT("wifi_setSSIDName() index=%d ssid='%s'", ssid_index, tmp);
    ret = wifi_setSSIDName(ssid_index, tmp);
    LOGT("wifi_setSSIDName index=%d ssid='%s' ret=%d", ssid_index, tmp,
            ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to set new SSID '%s'", ssid_ifname, tmp);
        return;
    }

    LOGI("%s: SSID updated to '%s'", ssid_ifname, tmp);
#ifndef CONFIG_RDK_DISABLE_SYNC
    if (!sync_send_ssid_change(ssid_index, ssid_ifname, vconf->ssid))
    {
        LOGE("%s: Failed to sync SSID change to '%s'", ssid_ifname, vconf->ssid);
    }
#endif
}

#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
static void set_security_legacy(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    MeshWifiAPSecurity sec;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    if (vconf->security_len == 0) return;

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
        return;
    }

    sec.index = ssid_index;

    LOGT("wifi_setApSecurityModeEnabled() index=%d mode='%s'", sec.index, sec.secMode);
    ret = wifi_setApSecurityModeEnabled(sec.index, sec.secMode);
    LOGT("wifi_setApSecurityModeEnabled() index=%d mode='%s' ret=%d", sec.index,
            sec.secMode, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to set new security mode to '%s'",
                ssid_ifname, sec.secMode);
        return;
    }

    if (strlen(sec.passphrase) == 0)
    {
        LOGW("%s: security_to_syncmsg returned empty sec.passphrase", ssid_ifname);
    }
    else
    {
        LOGT("wifi_setApSecurityKeyPassphrase() index=%d", sec.index);
        ret = wifi_setApSecurityKeyPassphrase(sec.index, sec.passphrase);
        LOGT("wifi_setApSecurityKeyPassphrase() index=%d ret=%d",
                ssid_index, ret);
        if (ret != RETURN_OK)
        {
            LOGW("%s: Failed to set new security passphrase", ssid_ifname);
        }
    }

    LOGI("%s: Security settings updated", ssid_ifname);

#ifndef CONFIG_RDK_DISABLE_SYNC
    if (!sync_send_security_change(ssid_index, ssid_ifname, &sec))
    {
        LOGW("%s: Failed to sync security change", ssid_ifname);
    }
#endif
}
#endif

static void set_enabled(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    LOGT("wifi_setSSIDEnable() index=%d enabled=%d", ssid_index, vconf->enabled);
    ret = wifi_setSSIDEnable(ssid_index, vconf->enabled);
    LOGT("wifi_setSSIDEnable() index=%d enabled=%d ret=%d", ssid_index,
            vconf->enabled, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to change enable to %d", ssid_ifname, vconf->enabled);
    }
}

static void set_ap_bridge(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    BOOL enable;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    enable = vconf->ap_bridge ? false : true;
    LOGT("wifi_setApIsolationEnable() index=%d enable=%d", ssid_index, enable);
    ret = wifi_setApIsolationEnable(ssid_index, enable);
    LOGT("wifi_setApIsolationEnable() index=%d enable=%d ret=%d", ssid_index,
            enable, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to change ap_bridge to %d", ssid_ifname, vconf->ap_bridge);
    }
}

static void set_rrm(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    LOGT("wifi_setNeighborReportActivation() index=%d rrm=%d", ssid_index,
            vconf->rrm);
    ret = wifi_setNeighborReportActivation(ssid_index, vconf->rrm);
    LOGT("wifi_setNeighborReportActivation() index=%d rrm=%d ret=%d", ssid_index,
            vconf->rrm, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to change rrm to %d", ssid_ifname, vconf->rrm);
    }
}

static void set_btm(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    LOGT("wifi_setBSSTransitionActivation() index=%d btm=%d", ssid_index,
            vconf->btm);
    ret = wifi_setBSSTransitionActivation(ssid_index, vconf->btm);
    LOGT("wifi_setBSSTransitionActivation() index=%d btm=%d ret=%d", ssid_index,
            vconf->btm, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to change btm to %d", ssid_ifname, vconf->btm);
    }
}

static void set_uapsd_enable(INT ssid_index, const struct schema_Wifi_VIF_Config *vconf)
{
    INT ret;
    const char *ssid_ifname = target_map_ifname((char *)vconf->if_name);

    LOGT("wifi_setApWmmUapsdEnable() index=%d uapsd_enable=%d", ssid_index,
            vconf->uapsd_enable);
    ret = wifi_setApWmmUapsdEnable(ssid_index, vconf->uapsd_enable);
    LOGT("wifi_setApWmmUapsdEnable() index=%d uapsd_enable=%d ret=%d", ssid_index,
            vconf->uapsd_enable, ret);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to change uapsd_enable to %d", ssid_ifname, vconf->uapsd_enable);
    }
}

bool target_vif_config_set2(
        const struct schema_Wifi_VIF_Config *vconf,
        const struct schema_Wifi_Radio_Config *rconf,
        const struct schema_Wifi_Credential_Config *cconfs,
        const struct schema_Wifi_VIF_Config_flags *changed,
        int num_cconfs)
{
    INT ssid_index;

    if (!vif_ifname_to_idx(target_map_ifname((char *)vconf->if_name), &ssid_index))
    {
        LOGE("%s: cannot get index for %s", __func__, target_map_ifname((char *)vconf->if_name));
        return false;
    }

    if (changed->enabled) set_enabled(ssid_index, vconf);
#ifdef CONFIG_RDK_LEGACY_SECURITY_SCHEMA
    if (changed->security) set_security_legacy(ssid_index, vconf);
#endif
    if (changed->ap_bridge) set_ap_bridge(ssid_index, vconf);
    if (changed->rrm) set_rrm(ssid_index, vconf);
    if (changed->btm) set_btm(ssid_index, vconf);
    if (changed->uapsd_enable) set_uapsd_enable(ssid_index, vconf);
    if (changed->ssid_broadcast) set_ssid_broadcast(ssid_index, vconf);
    if (changed->ssid) set_ssid(ssid_index, vconf);
    /* The 'bridge' field in VIF may be used as an input to hostapd config.
     * However, none of RDK HAL implementations actually use it as a parameter.
     * If any adjustment to bridge name is needed it's handled by the platform
     * itself as a part of initial configuration.
     * We keep track of this field (if set by cloud) for compatibility reasons.
     * If for any reason the bridge name should be applied, the new HAL should be
     * created for that purpose. For now, the value is stored in static variable.
     */
    if (changed->bridge) STRSCPY(vif_bridge_name, vconf->bridge);

    acl_apply(ssid_index, vconf);
#ifdef CONFIG_RDK_WPS_SUPPORT
    vif_config_set_wps(ssid_index, vconf, changed, rconf->if_name);
#endif

#ifdef CONFIG_RDK_MULTI_AP_SUPPORT
    vif_config_set_multi_ap(ssid_index, vconf->multi_ap, changed);
#endif

    set_security(ssid_index, vconf, changed);

    LOGT("wifi_applySSIDSettings() index=%d", ssid_index);
    if (wifi_applySSIDSettings(ssid_index) != RETURN_OK)
    {
        LOGW("Failed to apply SSID settings for index=%d", ssid_index);
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
