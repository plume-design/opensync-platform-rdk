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
 * wifihal_security.c
 *
 * RDKB Platform - Wifi HAL - Security Mapping
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
#include <ev.h>

#include <mesh/meshsync_msgs.h>

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

#define PSK_FILE_NAME           "/tmp/hostapd_%s.psk"

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

/*****************************************************************************/

typedef enum
{
    WIFIHAL_SEC_NONE                = 0,
    WIFIHAL_SEC_WEP_64,
    WIFIHAL_SEC_WEP_128,
    WIFIHAL_SEC_WPA_PERSONAL,
    WIFIHAL_SEC_WPA_ENTERPRISE,
    WIFIHAL_SEC_WPA2_PERSONAL,
    WIFIHAL_SEC_WPA2_ENTERPRISE,
    WIFIHAL_SEC_WPA_WPA2_PERSONAL,
    WIFIHAL_SEC_WPA_WPA2_ENTERPRISE
} wifihal_sec_type_t;

static c_item_t map_security[] = {
    C_ITEM_STR(WIFIHAL_SEC_NONE,                    "None"),
    C_ITEM_STR(WIFIHAL_SEC_WEP_64,                  "WEP-64"),
    C_ITEM_STR(WIFIHAL_SEC_WEP_128,                 "WEP-128"),
    C_ITEM_STR(WIFIHAL_SEC_WPA_PERSONAL,            "WPA-Personal"),
    C_ITEM_STR(WIFIHAL_SEC_WPA_ENTERPRISE,          "WPA-Enterprise"),
    C_ITEM_STR(WIFIHAL_SEC_WPA2_PERSONAL,           "WPA2-Personal"),
    C_ITEM_STR(WIFIHAL_SEC_WPA2_ENTERPRISE,         "WPA2-Enterprise"),
    C_ITEM_STR(WIFIHAL_SEC_WPA_WPA2_PERSONAL,       "WPA-WPA2-Personal"),
    C_ITEM_STR(WIFIHAL_SEC_WPA_WPA2_ENTERPRISE,     "WPA-WPA2-Enterprise")
};

/*****************************************************************************/

static char *
wifihal_security_conf_find_by_key(struct schema_Wifi_VIF_Config *vconf, char *key)
{
    int     i;

    for (i = 0; i < vconf->security_len; i++) {
        if (!strcmp(vconf->security_keys[i], key)) {
            return vconf->security[i];
        }
    }

    return NULL;
}

wifihal_key_t *
wifihal_security_key_find_by_id(wifihal_ssid_t *ssid, char *id)
{
    return (wifihal_key_t *)ds_tree_find(&ssid->keys, id);
}

static bool
wifihal_security_key_upsert(wifihal_ssid_t *ssid, char *key_id, char *psk, char *oftag)
{
    wifihal_key_t       *kp;

    kp = wifihal_security_key_find_by_id(ssid, key_id);
    if (!kp)
    {
        kp = calloc(1, sizeof(*kp));
        if (!kp) {
            LOGE("%s: Failed to allocate memory for new key '%s'", ssid->ifname, key_id);
            return false;
        }
        strncpy(kp->id, key_id, sizeof(kp->id)-1);
        ds_tree_insert(&ssid->keys, kp, kp->id);
    }

    if (psk) {
        strncpy(kp->psk, psk, sizeof(kp->psk)-1);
    }
    if (oftag) {
        strncpy(kp->oftag, oftag, sizeof(kp->oftag)-1);
    }

    return true;
}

static bool
wifihal_security_keys_update(wifihal_ssid_t *ssid)
{
    wifihal_key_t       *kp;
    FILE                *f1;
    char                buf[WIFIHAL_MAX_BUFFER];

    snprintf(buf, sizeof(buf)-1, PSK_FILE_NAME, ssid->ifname);
    f1 = fopen(buf, "wt");
    if (!f1) {
        LOGE("%s: Failed to write keys to '%s'", ssid->ifname, buf);
        return false;
    }

    ds_tree_foreach(&ssid->keys, kp)
    {
        if (strcmp(kp->id, OVSDB_SECURITY_KEY) == 0) {
            continue;
        }
        else if (strlen(kp->psk) == 0) {
            continue;
        }

        fprintf(f1, "00:00:00:00:00:00 %s\n", kp->psk);
    }

    fclose(f1);

#ifdef WPA_CLIENTS
    // Send reload command
    size_t len = sizeof(buf);
    bool ret = wifihal_wpactrl_request(ssid, "RELOAD_WPA_PSK", buf, &len);
    if (!ret || strncmp(buf, "OK", 2) != 0) {
        LOGE("%s: Failed to reload psk file", ssid->ifname);
        return false;
    }
#endif /* WPA_CLIENTS */

    return true;
}


/*****************************************************************************/

bool
wifihal_security_same(
        struct schema_Wifi_VIF_Config *vconf1,
        struct schema_Wifi_VIF_Config *vconf2)
{
    int             idx1;
    char            *val2;

    if (vconf1->security_len != vconf2->security_len) {
        return false;
    }

    for (idx1 = 0; idx1 < vconf1->security_len; idx1++)
    {
        val2 = wifihal_security_conf_find_by_key(vconf2, vconf1->security_keys[idx1]);

        if (val2 == NULL || strcmp(vconf1->security[idx1], val2))
        {
            return false;
        }
    }

    return true;
}

bool
wifihal_security_to_config(
        wifihal_ssid_t *ssid,
        struct schema_Wifi_VIF_Config *vconf)
{
    wifihal_sec_type_t              stype;
    wifihal_key_t                   *kp;
    c_item_t                        *citem;
    CHAR                            buf[WIFIHAL_MAX_BUFFER];
    CHAR                            radius_ip[WIFIHAL_MAX_BUFFER];
    CHAR                            radius_secret[WIFIHAL_MAX_BUFFER];
    UINT                            radius_port;
    int                             n = 0;
    INT                             ret;

    // security, security_keys, security_len
    WIFIHAL_TM_START();
    ret = wifi_getApSecurityModeEnabled(ssid->index, buf);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApSecurityModeEnabled(%d) ret \"%s\"",
                                        ssid->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK) {
        LOGE("%s: Failed to get security mode", ssid->ifname);
        return false;
    }

    if (!(citem = c_get_item_by_str(map_security, buf)))
    {
        LOGE("%s: Failed to decode security mode (%s)", ssid->ifname, buf);
        return false;
    }
    stype = (wifihal_sec_type_t)citem->key;

    // map: encryption
    strncpy(vconf->security_keys[n],
            OVSDB_SECURITY_ENCRYPTION,
            sizeof(vconf->security_keys[n])-1);
    switch (stype)
    {
    case WIFIHAL_SEC_NONE:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_ENCRYPTION_OPEN,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WEP_64:
    case WIFIHAL_SEC_WEP_128:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_ENCRYPTION_WEP,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WPA_PERSONAL:
    case WIFIHAL_SEC_WPA2_PERSONAL:
    case WIFIHAL_SEC_WPA_WPA2_PERSONAL:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_ENCRYPTION_WPA_PSK,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WPA_ENTERPRISE:
    case WIFIHAL_SEC_WPA2_ENTERPRISE:
    case WIFIHAL_SEC_WPA_WPA2_ENTERPRISE:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_ENCRYPTION_WPA_EAP,
                sizeof(vconf->security[n])-1);
        break;

    default:
        LOGE("%s: Unsupported security type (%d = %s)", ssid->ifname, stype, buf);
        return false;
    }
    n++;

    if (stype == WIFIHAL_SEC_NONE) {
        vconf->security_len = n;
        return true;
    }

    // map: mode
    strncpy(vconf->security_keys[n],
            OVSDB_SECURITY_MODE,
            sizeof(vconf->security_keys[n])-1);
    switch (stype)
    {
    case WIFIHAL_SEC_WEP_64:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_MODE_WEP64,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WEP_128:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_MODE_WEP128,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WPA_PERSONAL:
    case WIFIHAL_SEC_WPA_ENTERPRISE:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_MODE_WPA1,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WPA2_PERSONAL:
    case WIFIHAL_SEC_WPA2_ENTERPRISE:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_MODE_WPA2,
                sizeof(vconf->security[n])-1);
        break;

    case WIFIHAL_SEC_WPA_WPA2_PERSONAL:
    case WIFIHAL_SEC_WPA_WPA2_ENTERPRISE:
        strncpy(vconf->security[n],
                OVSDB_SECURITY_MODE_MIXED,
                sizeof(vconf->security[n])-1);
        break;

    default:
        LOGE("%s: Unsupported security mode (stype %d = %s)", ssid->ifname, stype, buf);
        return false;
    }
    n++;

    switch (stype)
    {
    case WIFIHAL_SEC_WEP_64:
    case WIFIHAL_SEC_WEP_128:
    case WIFIHAL_SEC_WPA_PERSONAL:
    case WIFIHAL_SEC_WPA2_PERSONAL:
    case WIFIHAL_SEC_WPA_WPA2_PERSONAL:
        WIFIHAL_TM_START();
        ret = wifi_getApSecurityKeyPassphrase(ssid->index, buf);
        if (strlen(buf) == 0)
        {
            // Try once again!!
            ret = wifi_getApSecurityKeyPassphrase(ssid->index, buf);
            if (strlen(buf) == 0) {
                ret = RETURN_ERR;
                LOGW("%s: wifi_getApSecurityKeyPassphrase returned empty ssid string", ssid->ifname);
            }
        }
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApSecurityKeyPassphrase(%d) ret \"%s\"",
                                              ssid->index, (ret == RETURN_OK) ? buf : "");
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to retrieve security passphrase", ssid->ifname);
            return false;
        }

        // map: key
        strncpy(vconf->security_keys[n],
                OVSDB_SECURITY_KEY,
                sizeof(vconf->security_keys[n])-1);
        strncpy(vconf->security[n],
                buf,
                sizeof(vconf->security[n])-1);
        n++;

        // map: rest of keys, and oftags
        ds_tree_foreach(&ssid->keys, kp) {
            if (strcmp(kp->id, OVSDB_SECURITY_KEY) == 0)
            {
                // Just need to add oftag
                if (strlen(kp->oftag) > 0)
                {
                    strncpy(vconf->security_keys[n],
                            OVSDB_SECURITY_OFTAG,
                            sizeof(vconf->security_keys[n])-1);
                    strncpy(vconf->security[n],
                            kp->oftag,
                            sizeof(vconf->security[n])-1);
                    n++;
                }
            }
            else
            {
                // Add key and oftag
                if (strlen(kp->psk) > 0)
                {
                    strncpy(vconf->security_keys[n],
                            kp->id,
                            sizeof(vconf->security_keys[n])-1);
                    strncpy(vconf->security[n],
                            kp->psk,
                            sizeof(vconf->security[n])-1);
                    n++;
                }

                if (strlen(kp->oftag) > 0)
                {
                    snprintf(vconf->security_keys[n],
                             sizeof(vconf->security_keys[n])-1,
                             "%s-%s", OVSDB_SECURITY_OFTAG, kp->id);
                    strncpy(vconf->security[n],
                            kp->oftag,
                            sizeof(vconf->security[n])-1);
                    n++;
                }
            }
        }
        break;

    case WIFIHAL_SEC_WPA_ENTERPRISE:
    case WIFIHAL_SEC_WPA2_ENTERPRISE:
    case WIFIHAL_SEC_WPA_WPA2_ENTERPRISE:
        WIFIHAL_TM_START();
        ret = wifi_getApSecurityRadiusServer(ssid->index, radius_ip, &radius_port, radius_secret);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApSecurityRadiusServer(%d) ret \"%s\", %u, \"%s\"",
                                             ssid->index, (ret == RETURN_OK) ? radius_ip : "",
                                             radius_port, (ret == RETURN_OK) ? radius_secret : "");
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to retrieve radius settings", ssid->ifname);
            return false;
        }

        // map: radius_server_ip
        strncpy(vconf->security_keys[n],
                OVSDB_SECURITY_RADIUS_SERVER_IP,
                sizeof(vconf->security_keys[n])-1);
        strncpy(vconf->security[n],
                radius_ip,
                sizeof(vconf->security[n])-1);
        n++;

        // map: radius_server_port
        strncpy(vconf->security_keys[n],
                OVSDB_SECURITY_RADIUS_SERVER_PORT,
                sizeof(vconf->security_keys[n])-1);
        snprintf(vconf->security[n],
                 sizeof(vconf->security[n])-1,
                 "%u", radius_port);
        n++;

        // map: radius_server_secret
        strncpy(vconf->security_keys[n],
                OVSDB_SECURITY_RADIUS_SERVER_SECRET,
                sizeof(vconf->security_keys[n])-1);
        strncpy(vconf->security[n],
                radius_secret,
                sizeof(vconf->security[n])-1);
        n++;
        break;

    default:
        LOGE("%s: Unsupported security key (stype %d = %s)", ssid->ifname, stype, buf);
        return false;
    }

    vconf->security_len = n;
    return true;
}

bool
wifihal_security_to_syncmsg(
        struct schema_Wifi_VIF_Config *vconf,
        MeshWifiAPSecurity *dest)
{
    char        *val;
    char        *sec_str;
    int         sec_type;
    int         i;

    val = wifihal_security_conf_find_by_key(vconf, OVSDB_SECURITY_ENCRYPTION);
    if (!val)
    {
        LOGW("%s: Security-to-MSGQ failed -- No encryption type", vconf->if_name);
        LOGW("%s: Dumping %d security elements:", vconf->if_name, vconf->security_len);
        for (i = 0; i < vconf->security_len; i++)
        {
            LOGW("%s: ... \"%s\" = \"%s\"",
                 vconf->if_name,
                 vconf->security_keys[i],
                 vconf->security[i]);
        }
        return false;
    }

    if (strcmp(val, OVSDB_SECURITY_ENCRYPTION_WPA_PSK)) {
        LOGW("%s: Security-to-MSGQ failed -- Encryption '%s' not supported",
             vconf->if_name, val);
        return false;
    }

    val = wifihal_security_conf_find_by_key(vconf, OVSDB_SECURITY_MODE);
    if (!val) {
        LOGW("%s: Security-to-MSGQ failed -- No mode found", vconf->if_name);
        return false;
    }

    if (!strcmp(val, OVSDB_SECURITY_MODE_WPA1))
    {
        sec_type = WIFIHAL_SEC_WPA_PERSONAL;
    }
    else if (!strcmp(val, OVSDB_SECURITY_MODE_WPA2))
    {
        sec_type = WIFIHAL_SEC_WPA2_PERSONAL;
    }
    else if (!strcmp(val, OVSDB_SECURITY_MODE_MIXED))
    {
        sec_type = WIFIHAL_SEC_WPA_WPA2_PERSONAL;
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

    val = wifihal_security_conf_find_by_key(vconf, OVSDB_SECURITY_KEY);
    if (!val) {
        LOGW("%s: Security-to-MSGQ failed -- No key found", vconf->if_name);
        return false;
    }

    strncpy(dest->passphrase, val, sizeof(dest->passphrase)-1);
    strncpy(dest->secMode, sec_str, sizeof(dest->secMode)-1);
    strncpy(dest->encryptMode, DEFAULT_ENC_MODE, sizeof(dest->encryptMode)-1);

    return true;
}

wifihal_key_t *
wifihal_security_key_find_by_psk(wifihal_ssid_t *ssid, char *psk)
{
    wifihal_key_t       *kp;

    ds_tree_foreach(&ssid->keys, kp)
    {
        if (!strcmp(psk, kp->psk)) {
            return kp;
        }
    }

    return NULL;
}

void
wifihal_security_key_cleanup(wifihal_ssid_t *ssid)
{
    ds_tree_iter_t      iter;
    wifihal_key_t       *kp;

    kp = ds_tree_ifirst(&iter, &ssid->keys);
    while (kp)
    {
        ds_tree_iremove(&iter);
        free(kp);
        kp = ds_tree_inext(&iter);
    }

    return;
}

bool
wifihal_security_key_from_conf(
        wifihal_ssid_t *ssid,
        struct schema_Wifi_VIF_Config *vconf)
{
    char    *id;
    int     i;

    // First, clean-up existing keys
    wifihal_security_key_cleanup(ssid);

    // Parse security MAP and store keys/tags
    for (i = 0; i < vconf->security_len; i++)
    {
        if (!strncmp(vconf->security_keys[i],
                     OVSDB_SECURITY_KEY,
                     strlen(OVSDB_SECURITY_KEY)))
        {
            if (!wifihal_security_key_upsert(
                        ssid,
                        vconf->security_keys[i],
                        vconf->security[i],
                        NULL))
            {
                // It reports error
                return false;
            }
        }
        else if (!strncmp(vconf->security_keys[i],
                          OVSDB_SECURITY_OFTAG,
                          strlen(OVSDB_SECURITY_OFTAG)))
        {
            id = vconf->security_keys[i] + strlen(OVSDB_SECURITY_OFTAG);
            if (*id == '\0')
            {
                id = OVSDB_SECURITY_KEY;
            }
            else if (*id == '-')
            {
                id++;
            }
            if (!wifihal_security_key_upsert(
                        ssid,
                        id,
                        NULL,
                        vconf->security[i]))
            {
                // It reports error
                return false;
            }
        }
    }

    return wifihal_security_keys_update(ssid);
}
