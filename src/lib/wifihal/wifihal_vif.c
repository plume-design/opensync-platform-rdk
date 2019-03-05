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
 * wifihal_vif.c
 *
 * RDKB Platform - Wifi HAL - VIF API's
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

#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"
#include "evsched.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID   LOG_MODULE_ID_HAL

#define WIFIHAL_VIF_CONF_UPDATE_DELAY    10  // seconds
#define WIFIHAL_VIF_STATE_UPDATE_DELAY    1  // seconds

/*****************************************************************************/

typedef enum
{
    HW_MODE_11B         = 0,
    HW_MODE_11G,
    HW_MODE_11A,
    HW_MODE_11N,
    HW_MODE_11AC
} hw_mode_t;

static c_item_t map_hw_mode[] = {
    C_ITEM_STR(HW_MODE_11B,             "11b"),
    C_ITEM_STR(HW_MODE_11G,             "11g"),
    C_ITEM_STR(HW_MODE_11A,             "11a"),
    C_ITEM_STR(HW_MODE_11N,             "11n"),
    C_ITEM_STR(HW_MODE_11AC,            "11ac")
};

static c_item_t map_enable_disable[] = {
    C_ITEM_STR(true,                    "enabled"),
    C_ITEM_STR(false,                   "disabled")
};

static wifihal_vif_state_cb_t   *wifihal_vif_state_cb = NULL;
static wifihal_vif_config_cb_t  *wifihal_vif_config_cb = NULL;

/*****************************************************************************/

#ifdef WAR_VIF_DISABLE
/*
 * Workaround for VIF disable
 * This is temporarily required as the current CCSP wifi_hal using QCA
 * does not properly support enabling/disabling of a VAP
 */
#define SSID_DISABLED_SUFFIX        ".off"

static bool
wifihal_vif_essid_is_control(wifihal_ssid_t *ssid)
{
    char    *unmapped_ifname = target_unmap_ifname(ssid->ifname);

    // Only do this for backhaul SSIDs
    if (!strncmp(unmapped_ifname, "bhaul-ap-", 9))
    {
        return true;
    }

    return false;
}

static bool
wifihal_vif_essid_disabled(wifihal_ssid_t *ssid)
{
    char    active_essid[WIFIHAL_MAX_BUFFER];
    char    *p;
    INT     ret;

    // Only do this for certain VIFs
    if (!wifihal_vif_essid_is_control(ssid))
    {
        return false;
    }

    WIFIHAL_TM_START();
    ret = wifi_getSSIDNameStatus(ssid->index, active_essid);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDNameStatus(%d) ret \"%s\"",
                                 ssid->index, active_essid);
    if (ret != RETURN_OK) {
        LOGW("%s: Failed to get active SSID for disable check", ssid->ifname);
        return false;
    }

    if (strlen(active_essid) <= strlen(SSID_DISABLED_SUFFIX))
    {
        return false;
    }

    p = active_essid + (strlen(active_essid) - strlen(SSID_DISABLED_SUFFIX));
    if (!strcmp(p, SSID_DISABLED_SUFFIX))
    {
        return true;
    }

    return false;
}

static bool
wifihal_vif_essid_trunc(wifihal_ssid_t *ssid, char *essid)
{
    char    *p;

    // Only do this for certain VIFs
    if (!wifihal_vif_essid_is_control(ssid))
    {
        return false;
    }

    if (strlen(essid) <= strlen(SSID_DISABLED_SUFFIX))
    {
        return false;
    }

    p = essid + (strlen(essid) - strlen(SSID_DISABLED_SUFFIX));

    if (!strcmp(p, SSID_DISABLED_SUFFIX))
    {
        *p = '\0';
        return true;
    }

    return false;
}

static bool
wifihal_vif_essid_control(wifihal_ssid_t *ssid, bool enable)
{
    char    essid[WIFIHAL_MAX_BUFFER];
    char    *p;
    INT     ret;

    // Only do this for certain VIFs
    if (!wifihal_vif_essid_is_control(ssid)) {
        return false;
    }

    WIFIHAL_TM_START();
    ret = wifi_getSSIDNameStatus(ssid->index, essid);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDNameStatus(%d) ret \"%s\"",
                                 ssid->index, essid);
    if (ret != RETURN_OK) {
        LOGW("%s: Failed to get active SSID for control", ssid->ifname);
        return false;
    }

    if (enable)
    {
        // Enable the ESSID
        if (strlen(essid) < strlen(SSID_DISABLED_SUFFIX))
        {
            return false;
        }

        p = essid + (strlen(essid) - strlen(SSID_DISABLED_SUFFIX));
        if (strcmp(p, SSID_DISABLED_SUFFIX))
        {
            // Not disabled
            return false;
        }

        *p = '\0';
    }
    else
    {
        // Disable the ESSID
        if ((strlen(essid) + strlen(SSID_DISABLED_SUFFIX)) > (sizeof(essid)-1))
        {
            return false;
        }

        if (strlen(essid) > strlen(SSID_DISABLED_SUFFIX))
        {
            p = essid + (strlen(essid) - strlen(SSID_DISABLED_SUFFIX));
            if (!strcmp(p, SSID_DISABLED_SUFFIX)) {
                // Already disabled
                return false;
            }
        }

        strcat(essid, SSID_DISABLED_SUFFIX);
    }

    WIFIHAL_TM_START();
    ret = wifi_pushSSID(ssid->index, essid);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_pushSSID(%d, \"%s\")",
                        ssid->index, essid);
    LOGD("[WIFI_HAL SET] wifi_pushSSID(%d, \"%s\") = %d",
                        ssid->index, essid, ret);
    if (ret != RETURN_OK) {
        LOGE("%s: Failed to push %s SSID '%s'", ssid->ifname, enable ? "enabled" : "disabled", essid);
        return false;
    }

    if (!wifihal_sync_send_ssid_change(ssid, essid)) {
        LOGW("%s: Failed to sync SSID change to '%s'", ssid->ifname, essid);
    }

    return true;
}

void wifihal_vif_essid_get_enabled_name(char *tmp, int size, char *ssid, bool enabled)
{
    if (enabled) {
        snprintf(tmp, size-1, "%s", ssid);
    } else {
        snprintf(tmp, size-1, "%s%s", ssid, SSID_DISABLED_SUFFIX);
    }
}

#endif /* WAR_VIF_DISABLE */

/*****************************************************************************/

static void
wifihal_vif_conf_update_task(void *arg)
{
    struct schema_Wifi_VIF_Config   vconf;
    wifihal_ssid_t                  *ssid = arg;

    if (!wifihal_vif_config_cb) {
        LOGW("%s: update task run without any config callback!", ssid->ifname);
        return;
    }

    if (!wifihal_vif_config_get(ssid->ifname, &vconf))
    {
        LOGE("%s: update task failed -- unable to retrieve config", ssid->ifname);
        return;
    }

    LOGN("%s: Updating configuration...", ssid->ifname);
    wifihal_vif_config_cb(&vconf, NULL);

    return;
}

static void
wifihal_vif_state_update_task(void *arg)
{
    wifihal_ssid_t                  *ssid = arg;

    LOGN("%s: Updating state...", ssid->ifname);
    wifihal_vif_state_update(ssid);

    return;
}

/*****************************************************************************/

bool
wifihal_vif_is_enabled(wifihal_ssid_t *ssid)
{
    BOOL        enabled = false;
    INT         ret;

    WIFIHAL_TM_START();
    ret = wifi_getSSIDEnable(ssid->index, &enabled);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDEnable(%d) ret %s",
                             ssid->index, enabled ? "true" : "false");
    if (ret != RETURN_OK) {
        LOGW("%s: failed to get SSIDEnable, assuming false", ssid->ifname);
        enabled = false;
    }
#ifdef WAR_VIF_DISABLE
    else if (enabled)
    {
        if (wifihal_vif_essid_disabled(ssid)) {
            enabled = false;
        }
    }
#endif /* WAR_VIF_DISABLE */

    return enabled;
}

bool
wifihal_vif_state_register(char *ifname, wifihal_vif_state_cb_t *vif_state_cb)
{
    (void)ifname;
    wifihal_vif_state_cb = vif_state_cb;

    return true;
}

bool
wifihal_vif_config_register(const char *ifname, void *vconf_cb)
{
    (void)ifname;
    wifihal_vif_config_cb = vconf_cb;

    return true;
}

bool
wifihal_vif_config_get(const char *ifname, struct schema_Wifi_VIF_Config *vconf)
{
    LOGT("%s(%s)", __FUNCTION__, ifname);
    wifihal_ssid_t      *ssid;
    hw_mode_t           min_hw_mode;
    CHAR                buf[WIFIHAL_MAX_BUFFER], tmp[WIFIHAL_MAX_BUFFER];
    BOOL                bval, gOnly, nOnly, acOnly;
    char                *str;
    INT                 ret;

    if (!(ssid = wifihal_ssid_by_ifname(ifname)))
    {
        LOGW("%s: Get config failed -- VIF/SSID not found", ifname);
        return false;
    }

    memset(vconf, 0, sizeof(*vconf));

    // if_name
    strncpy(vconf->if_name,
            target_unmap_ifname(ssid->ifname),
            sizeof(vconf->if_name)-1);

    // mode (w/ exists)
    strncpy(vconf->mode, "ap", sizeof(vconf->mode)-1);
    vconf->mode_exists = true;

    // enabled (w/ exists)
    vconf->enabled = wifihal_vif_is_enabled(ssid);
    vconf->enabled_exists = true;

    // bridge (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getApBridgeInfo(ssid->index, buf, tmp, tmp);
    WIFIHAL_TM_STOP_NORET(WIFIHAL_STD_TIME, "wifi_getApBridgeInfo(%d) ret \"%s\" = %d",
                               ssid->index, (ret == RETURN_OK) ? buf : "", ret);
    if (ret == RETURN_OK) {
        if (ssid->index == 0 || ssid->index == 1) {
            strcpy(vconf->bridge, "br-home");
        } else {
            strncpy(vconf->bridge,
                    target_unmap_ifname(buf),
                    sizeof(vconf->bridge)-1);
        }
        vconf->bridge_exists = true;
    }

    // vlan_id (w/ exists)
    if ((vconf->vlan_id = target_map_ifname_to_vlan(vconf->if_name)) > 0)
    {
        vconf->vlan_id_exists = true;
    }

    // wds (w/ exists)
    vconf->wds = false;
    vconf->wds_exists = true;

    // ap_bridge (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getApIsolationEnable(ssid->index, &bval);
    WIFIHAL_TM_STOP_NORET(WIFIHAL_STD_TIME, "wifi_getApIsolationEnable(%d) ret \"%s\" = %d",
                                    ssid->index, bval ? "true" : "false", ret);
    if (ret == RETURN_OK)
    {
        vconf->ap_bridge = bval ? false : true;
        vconf->ap_bridge_exists = true;
    }

    // vif_radio_idx (w/ exists)
    vconf->vif_radio_idx = target_map_ifname_to_vif_radio_idx(vconf->if_name);
    vconf->vif_radio_idx_exists = true;

    // ssid_broadcast (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getApSsidAdvertisementEnable(ssid->index, &bval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApSsidAdvertisementEnable(%d) ret %s",
                                            ssid->index, bval ? "true" : "false");
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get SSID Advertisement Enable", ssid->ifname);
    }
    else
    {
        str = c_get_str_by_key(map_enable_disable, bval);
        if (strlen(str) == 0)
        {
            LOGW("%s: failed to decode ssid_enable (%d)",
                 ssid->ifname, bval);
        }
        else
        {
            strncpy(vconf->ssid_broadcast, str, sizeof(vconf->ssid_broadcast)-1);
            vconf->ssid_broadcast_exists = true;
        }
    }

    // ssid (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getSSIDName(ssid->index, buf);
    if (strlen(buf) == 0)
    {
        // Try once again!!
        ret = wifi_getSSIDName(ssid->index, buf);
        if (strlen(buf) == 0) {
            ret = RETURN_ERR;
            LOGW("%s: wifi_getSSIDName returned empty ssid string", ssid->ifname);
        }
    }
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDName(%d) ret \"%s\"",
                           ssid->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK)
    {
        LOGW("%s: failed to get SSID", ssid->ifname);
    }
    else
    {
        strncpy(vconf->ssid, buf, sizeof(vconf->ssid)-1);
#ifdef WAR_VIF_DISABLE
        wifihal_vif_essid_trunc(ssid, vconf->ssid);
#endif /* WAR_VIF_DISABLE */
        vconf->ssid_exists = true;
    }

    // security, security_keys, security_len
    if (wifihal_security_to_config(ssid, vconf) == false)
    {
        // It reports errors
        return false;
    }

    // mac_list_type (w/ exists)
    // mac_list, mac_list_len
    if (wifihal_acl_to_config(ssid, vconf) == false)
    {
        // It reports errors
        return false;
    }

    // min_hw_mode (w/ exists)
    if (ssid->radio->band[0] == '5') {
        min_hw_mode = HW_MODE_11A;
    } else {
        min_hw_mode = HW_MODE_11B;
    }
    WIFIHAL_TM_START();
    ret = wifi_getRadioStandard(ssid->radio->index, buf, &gOnly, &nOnly, &acOnly);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioStandard(%d) ret (\"%s\", %d, %d, %d)",
                                ssid->index, (ret == RETURN_OK) ? buf : "",
                                                         gOnly, nOnly, acOnly);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get min_hw_mode from %s",
             ssid->ifname, ssid->radio->ifname);
    }
    else
    {
        if (gOnly) {
            min_hw_mode = HW_MODE_11G;
        } else if (nOnly) {
            min_hw_mode = HW_MODE_11N;
        } else if (acOnly) {
            min_hw_mode = HW_MODE_11AC;
        }
    }

    str = c_get_str_by_key(map_hw_mode, min_hw_mode);
    if (strlen(str) == 0)
    {
        LOGW("%s: failed to encode min_hw_mode (%d)",
             ssid->ifname, min_hw_mode);
    }
    else
    {
        strncpy(vconf->min_hw_mode,
                str,
                sizeof(vconf->min_hw_mode)-1);
        vconf->min_hw_mode_exists = true;
    }

    return true;
}

bool
wifihal_vif_state_get(const char *ifname, struct schema_Wifi_VIF_State *vstate,
        schema_filter_t *filter)
{
    struct schema_Wifi_VIF_Config   vconf;
    wifihal_ssid_t                  *ssid;
    ULONG                           lval;
    CHAR                            buf[WIFIHAL_MAX_BUFFER];
    INT                             ret;
    int                             i;

    if (!(ssid = wifihal_ssid_by_ifname(ifname)))
    {
        LOGW("%s: Get state failed -- VIF/SSID not found", ifname);
        return false;
    }

    // Most is copied from config
    if (!wifihal_vif_config_get(ifname, &vconf))
    {
        LOGW("%s: Get state failed -- unable to get config", ifname);
        return false;
    }

    memset(vstate, 0, sizeof(*vstate));
    memset(filter, 0, sizeof(*filter));
    schema_filter_init(filter,  "+");

#define VIF_STATE_COPY_OPT(TYPE, FIELD) \
    if (vconf.FIELD##_exists) { \
        SCHEMA_FF_SET_##TYPE(filter, vstate, FIELD, vconf.FIELD); \
    }

#define VIF_STATE_COPY_REQ(TYPE, FIELD) \
    SCHEMA_FF_SET_##TYPE##_REQ(filter, vstate, FIELD, vconf.FIELD); \

    // Copy from config

    VIF_STATE_COPY_REQ(STR, if_name);
    VIF_STATE_COPY_OPT(STR, mode);
    VIF_STATE_COPY_OPT(INT, enabled);
    VIF_STATE_COPY_OPT(STR, bridge);
    VIF_STATE_COPY_OPT(INT, vlan_id);
    VIF_STATE_COPY_OPT(INT, ap_bridge);
    VIF_STATE_COPY_OPT(INT, wds);
    VIF_STATE_COPY_OPT(INT, vif_radio_idx);
    VIF_STATE_COPY_OPT(STR, ssid_broadcast);
    VIF_STATE_COPY_OPT(STR, min_hw_mode);

    // security, security_keys, security_len
    for (i = 0; i < vconf.security_len; i++)
    {
        strncpy(vstate->security_keys[i], vconf.security_keys[i], sizeof(vstate->security_keys[i])-1);
        strncpy(vstate->security[i],      vconf.security[i],      sizeof(vstate->security[i])-1);
    }
    vstate->security_len = vconf.security_len;
    schema_filter_add(filter, SCHEMA_COLUMN(Wifi_VIF_State, security));

    VIF_STATE_COPY_OPT(STR, mac_list_type);

    // mac_list, mac_list_len
    for (i = 0; i < vconf.mac_list_len; i++)
    {
        strncpy(vstate->mac_list[i], vconf.mac_list[i], sizeof(vstate->mac_list[i])-1);
    }
    vstate->mac_list_len = vconf.mac_list_len;
    schema_filter_add(filter, SCHEMA_COLUMN(Wifi_VIF_State, mac_list));

    // Extra info besides config

    // ssid (w/ exists)
    memset(buf, 0, sizeof(buf));
    WIFIHAL_TM_START();
    ret = wifi_getSSIDNameStatus(ssid->index, buf);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDNameStatus(%d) ret \"%s\"",
                                 ssid->index, buf);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get active SSID", ssid->ifname);
    }
    else
    {
        strncpy(vstate->ssid, buf, sizeof(vstate->ssid)-1);
#ifdef WAR_VIF_DISABLE
        wifihal_vif_essid_trunc(ssid, vstate->ssid);
#endif
        vstate->ssid_exists = true;
        schema_filter_add(filter, SCHEMA_COLUMN(Wifi_VIF_State, ssid));
    }

    // mac (w/ exists)
    memset(buf, 0, sizeof(buf));
    WIFIHAL_TM_START();
    ret = wifi_getBaseBSSID(ssid->index, buf);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getBaseBSSID(%d) ret \"%s\"",
                            ssid->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get base BSSID (mac)", ssid->ifname);
    }
    else
    {
        SCHEMA_FF_SET_STR(filter, vstate, mac, buf);
    }

    // channel (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioChannel(ssid->radio->index, &lval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioChannel(%d) ret %ld",
                               ssid->index, lval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get channel from %s", ssid->ifname, ssid->radio->ifname);
    }
    else
    {
        SCHEMA_FF_SET_INT(filter, vstate, channel, lval);
    }

    return true;
}

bool
wifihal_vif_state_update(wifihal_ssid_t *ssid)
{
    struct schema_Wifi_VIF_State    vstate;
    schema_filter_t filter;

    if (!wifihal_vif_state_cb) {
        return true;
    }

    if (!wifihal_vif_state_get(ssid->ifname, &vstate, &filter))
    {
        LOGE("%s: VIF state update failed -- unable to get state", ssid->ifname);
        return false;
    }

    wifihal_vif_state_cb(&vstate, &filter);

    return true;
}

bool
wifihal_vif_conf_updated(wifihal_ssid_t *ssid)
{
    // Cancel any previously scheduled update task
    evsched_task_cancel_by_find(
            wifihal_vif_conf_update_task,
            ssid,
            (EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG));

    // Schedule update task
    evsched_task(
            wifihal_vif_conf_update_task,
            ssid,
            EVSCHED_SEC(WIFIHAL_VIF_CONF_UPDATE_DELAY));

    return true;
}

bool
wifihal_vif_conf_all_updated(wifihal_radio_t *radio)
{
    wifihal_ssid_t      *ssid;

    ds_dlist_foreach(&radio->ssids, ssid)
    {
        wifihal_vif_conf_updated(ssid);
    }

    return true;
}

bool
wifihal_vif_state_updated(wifihal_ssid_t *ssid)
{
    // Cancel any previously scheduled update task
    evsched_task_cancel_by_find(
            wifihal_vif_state_update_task,
            ssid,
            (EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG));

    // Schedule update task
    evsched_task(
            wifihal_vif_state_update_task,
            ssid,
            EVSCHED_SEC(WIFIHAL_VIF_STATE_UPDATE_DELAY));

    return true;
}

#if defined(HOSTAPD_RESTART)
extern INT wifi_restartHostApd(void);
#endif

bool
wifihal_vif_update(const char *ifname, struct schema_Wifi_VIF_Config *vconf)
{
    struct schema_Wifi_VIF_Config   cur_vconf;
    MeshWifiAPSecurity              sec;
    wifihal_ssid_t                  *ssid;
    c_item_t                        *citem;
    char                            tmp[256];
    BOOL                            bval;
    INT                             ret;

    if (!(ssid = wifihal_ssid_by_ifname(ifname)))
    {
        LOGW("%s: Update VIF failed -- VIF/SSID not found", ifname);
        return false;
    }

    if (!wifihal_vif_config_get(ifname, &cur_vconf))
    {
        LOGE("%s: wifihal_vif_update() failed -- unable to get current config", ifname);
        return false;
    }

#define BOTH_HAS(x)     (vconf->x##_exists && cur_vconf.x##_exists)
#define CHANGED(x)      (BOTH_HAS(x) && vconf->x != cur_vconf.x)
#define STR_CHANGED(x)  (BOTH_HAS(x) && strcmp(vconf->x, cur_vconf.x))

    // Always apply ACL
    if (!wifihal_acl_from_config(ssid, vconf))
    {
        // It reports error
        return false;
    }

    if (CHANGED(vlan_id))
    {
        if (!target_map_update_vlan(vconf->if_name, vconf->vlan_id))
        {
            LOGE("%s: Failed to update VLAN to %u", ssid->ifname, vconf->vlan_id);
            return false;
        }
        else
        {
            LOGI("%s: Updated VLAN to %u", ssid->ifname, vconf->vlan_id);
        }
    }

    if (CHANGED(ssid_broadcast))
    {
        if ((citem = c_get_item_by_str(map_enable_disable, vconf->ssid_broadcast)))
        {
            bval = citem->key ? TRUE : FALSE;
            WIFIHAL_TM_START();
            ret = wifi_setApSsidAdvertisementEnable(ssid->index, bval);
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApSsidAdvertisementEnable(%d, %d)",
                                                    ssid->index, bval);
            LOGD("[WIFI_HAL SET] wifi_setApSsidAdvertisementEnable(%d, %d) = %d",
                                                    ssid->index, bval, ret);
            if (ret != RETURN_OK)
            {
                LOGE("%s: Failed to set SSID Broadcast to '%d'", ssid->ifname, bval);
            }
            else
            {
                LOGI("%s: Updated SSID Broadcast to %d", ssid->ifname, bval);
            }
        }
        else
        {
            LOGW("%s: Failed to decode ssid_broadcast \"%s\"",
                 ssid->ifname, vconf->ssid_broadcast);
        }
    }

    if (strlen(vconf->ssid) == 0)
    {
        LOGW("%s: vconf->ssid string is empty", ssid->ifname);
    }
    else if (STR_CHANGED(ssid))
    {
#ifdef WAR_VIF_DISABLE
        wifihal_vif_essid_get_enabled_name(tmp, sizeof(tmp), vconf->ssid, cur_vconf.enabled);
#else
        snprintf(tmp, sizeof(tmp)-1, "%s", vconf->ssid);
#endif
        WIFIHAL_TM_START();
        ret = wifi_setSSIDName(ssid->index, tmp);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setSSIDName(%d, \"%s\")",
                               ssid->index, tmp);
        LOGD("[WIFI_HAL SET] wifi_setSSIDName(%d, \"%s\") = %d",
                               ssid->index, tmp, ret);
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to set new SSID '%s'", ssid->ifname, tmp);
            return false;
        }

        WIFIHAL_TM_START();
        ret = wifi_pushSSID(ssid->index, tmp);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_pushSSID(%d, \"%s\")",
                            ssid->index, tmp);
        LOGD("[WIFI_HAL SET] wifi_pushSSID(%d, \"%s\") = %d",
                            ssid->index, tmp, ret);
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to push new SSID '%s'", ssid->ifname, tmp);
            return false;
        }

        LOGI("%s: SSID updated to '%s'", ssid->ifname, tmp);

        if (!wifihal_sync_send_ssid_change(ssid, vconf->ssid))
        {
            LOGE("%s: Failed to sync SSID change to '%s'", ssid->ifname, vconf->ssid);
            return false;
        }
    }

    /*
     * !!! XXX WAR !!! Currently upper layer only provides changed values.
     * Currently, a map length of 0 could mean two things: it hasn't changed,
     * or it's actually empty.  Since we can't tell the difference at the moment,
     * we have to assume it hasn't changed.
     */
    if (vconf->security_len > 0 && !wifihal_security_same(&cur_vconf, vconf))
    {
        if (!wifihal_security_key_from_conf(ssid, vconf))
        {
            // It reports error
            return false;
        }
        memset(&sec, 0, sizeof(sec));
        if (!wifihal_security_to_syncmsg(vconf, &sec))
        {
            LOGE("%s: Failed to convert security for sync", ssid->ifname);
            return false;
        }
        sec.index = ssid->index;

        WIFIHAL_TM_START();
        ret = wifi_setApSecurityModeEnabled(sec.index, sec.secMode);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApSecurityModeEnabled(%d, \"%s\")",
                                            sec.index, sec.secMode);
        LOGD("[WIFI_HAL SET] wifi_setApSecurityModeEnabled(%d, \"%s\") = %d",
                                            sec.index, sec.secMode, ret);
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to set new security mode to '%s'",
                 ssid->ifname, sec.secMode);
            return false;
        }

        if (strlen(sec.passphrase) == 0)
        {
            LOGW("%s: wifihal_security_to_syncmsg returned empty sec.passphrase", ssid->ifname);
        }
        else
        {
            WIFIHAL_TM_START();
            ret = wifi_setApSecurityKeyPassphrase(sec.index, sec.passphrase);
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApSecurityKeyPassphrase(%d, \"%s\")",
                                                  sec.index, sec.passphrase);
            LOGD("[WIFI_HAL SET] wifi_setApSecurityKeyPassphrase(%d, \"%s\") = %d",
                                                  sec.index, sec.passphrase, ret);
            if (ret != RETURN_OK) {
                LOGE("%s: Failed to set new security passphrase", ssid->ifname);
                return false;
            }
        }

        if (sec.index > 1)
        {
            // Request hostapd config generation now
            WIFIHAL_TM_START();
            wifi_createHostApdConfig(sec.index, 0 /* not wps */);
            WIFIHAL_TM_STOP(RETURN_OK, WIFIHAL_STD_TIME, "wifi_createHostApdConfig(%d, 0)",
                                     sec.index);
        }

#if defined(HOSTAPD_RECONFIG)
        LOGD("[WIFI_HAL SET] Calling \"hostapd_cli -i %s reconfig\"", ssid->ifname);
        snprintf(ARRAY_AND_SIZE(tmp), "/sbin/hostapd_cli -i %s reconfig", ssid->ifname);
        ret = system(tmp);
        if (WEXITSTATUS(ret) != 0) {
            LOGW("%s: hostapd_cli failed to apply security change (rc = %d)",
                 ssid->ifname, WEXITSTATUS(ret));
        }
#elif defined(HOSTAPD_RESTART)
        WIFIHAL_TM_START();
        ret = wifi_restartHostApd();
        WIFIHAL_TM_STOP(RETURN_OK, WIFIHAL_STD_TIME, "wifi_restartHostApd() = %d", ret);
        LOGD("[WIFI_HAL SET] wifi_restartHostApd() = %d", ret);
        if (ret != RETURN_OK) {
            LOGW("%s: Failed to restart hostapd to apply security change", ssid->ifname);
        }
#elif defined(BCM_WIFI)
        WIFIHAL_TM_START();
        ret = wifi_stopHostApd();
        WIFIHAL_TM_STOP(RETURN_OK, WIFIHAL_STD_TIME, "wifi_stopHostApd() = %d", ret);
        LOGD("[WIFI_HAL SET] wifi_stopHostApd() = %d", ret);
        if (ret != RETURN_OK) {
            LOGW("%s: Failed to stop hostapd to apply security change", ssid->ifname);
        }
        WIFIHAL_TM_START();
        ret = wifi_startHostApd();
        WIFIHAL_TM_STOP(RETURN_OK, WIFIHAL_STD_TIME, "wifi_startHostApd() = %d", ret);
        LOGD("[WIFI_HAL SET] wifi_startHostApd() = %d", ret);
        if (ret != RETURN_OK) {
            LOGW("%s: Failed to start hostapd to apply security change", ssid->ifname);
        }
#endif

        LOGI("%s: Security settings updated", ssid->ifname);

        if (!wifihal_sync_send_security_change(ssid, &sec))
        {
            LOGE("%s: Failed to sync security change", ssid->ifname);
            return false;
        }
    }

    if (CHANGED(enabled)) {
#ifdef WAR_VIF_DISABLE
        if (!wifihal_vif_essid_control(ssid, vconf->enabled))
#else /* not WAR_VIF_DISABLE */
        WIFIHAL_TM_START();
        ret = wifi_setSSIDEnable(ssid->index, vconf->enabled);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setSSIDEnable(%d, %s)",
                                 ssid->index, vconf->enabled ? "true" : "false");
        LOGD("[WIFI_HAL SET] wifi_setSSIDEnable(%d, %d) = %d",
                                 ssid->index, vconf->enabled, ret);
        if (ret != RETURN_OK)
#endif /* not WAR_VIF_DISABLE */
        {
            LOGE("%s: Failed to change enable to %d", ssid->ifname, vconf->enabled);
            return false;
        }

        ssid->enabled = vconf->enabled;
    }

    if (CHANGED(ap_bridge))
    {
        WIFIHAL_TM_START();
        ret = wifi_setApIsolationEnable(ssid->index, vconf->ap_bridge ? false : true);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApIsolationEnable(%d, %s)",
                                        ssid->index, vconf->ap_bridge ? "false" : "true");
        LOGD("[WIFI_HAL SET] wifi_setApIsolationEnable(%d, %d) = %d",
                                        ssid->index, !vconf->ap_bridge, ret);
        if (ret != RETURN_OK) {
            LOGE("%s: Failed to change ap_bridge to %d", ssid->ifname, vconf->ap_bridge);
            return false;
        }
    }

#undef BOTH_HAS
#undef CHANGED
#undef STR_CHANGED

    if (!wifihal_vif_state_updated(ssid))
    {
        return false;
    }
    return true;
}
