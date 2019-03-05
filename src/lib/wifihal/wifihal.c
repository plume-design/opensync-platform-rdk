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
 * wifihal.c
 *
 * RDKB Platform - Wifi HAL
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
#include "evsched.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"
#include "devinfo.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID               LOG_MODULE_ID_HAL

#define REDIR_ADDR_MAX          256

// CSA deauth flags.  Possible values are:
//      WIFI_CSA_DEAUTH_MODE_NONE
//      WIFI_CSA_DEAUTH_MODE_UCAST
//      WIFI_CSA_DEAUTH_MODE_BCAST
#define CSA_DEAUTH_FLAG(x)      WIFI_CSA_DEAUTH_MODE_##x

#define CSA_2G_BHAUL_DEAUTH     CSA_DEAUTH_FLAG(NONE)
#define CSA_2G_OTHER_DEAUTH     CSA_DEAUTH_FLAG(BCAST)

#define CSA_5G_BHAUL_DEAUTH     CSA_DEAUTH_FLAG(NONE)
#define CSA_5G_OTHER_DEAUTH     CSA_DEAUTH_FLAG(NONE)

#define CSA_DEAUTH_DEFAULT      CSA_DEAUTH_FLAG(NONE)

/*****************************************************************************/

struct ev_loop              *wifihal_evloop = NULL;

#ifdef WIFIHAL_TIME_CALLS
ev_tstamp                   __wifihal_tstamp;
#endif

/*****************************************************************************/

typedef void (*wifihal_devconf_cb_t)(
                struct schema_AWLAN_Node *awlan,
                schema_filter_t *filter);

static wifihal_devconf_cb_t wifihal_devconf_cb = NULL;
static ds_dlist_t           wifihal_radios;

/*****************************************************************************/

static bool
wifihal_csa_deauth_set(wifihal_ssid_t *ssid)
{
    wifihal_radio_t     *radio = ssid->radio;
    uint32_t            deauth_flags = CSA_DEAUTH_DEFAULT;
    char                *unmapped_ifname = target_unmap_ifname(ssid->ifname);

    if (!strncmp(unmapped_ifname, "bhaul-", 6))
    {
        if (radio->band[0] == '2') {
            deauth_flags = CSA_2G_BHAUL_DEAUTH;  // Backhaul 2.4G
        } else if (radio->band[0] == '5') {
            deauth_flags = CSA_5G_BHAUL_DEAUTH;  // Backhaul 5G
        }
    }
    else
    {
        if (radio->band[0] == '2') {
            deauth_flags = CSA_2G_OTHER_DEAUTH;  // Other 2.4G
        } else if (radio->band[0] == '5') {
            deauth_flags = CSA_5G_OTHER_DEAUTH;  // Other 5G
        }
    }

    WIFIHAL_TM_START();
    int ret = wifi_setApCsaDeauth(ssid->index, deauth_flags);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApCsaDeauth(%d, 0x%02X)",
                                  ssid->index, deauth_flags);
    if (ret != RETURN_OK) {
        LOGW("%s: Failed to set CSA deauth flags to 0x%02X", ssid->ifname, deauth_flags);
        return false;
    }

    LOGI("%s: CSA deauth flags set to 0x%02X", ssid->ifname, deauth_flags);

    return true;
}

static bool
wifihal_scanfilter_set(wifihal_ssid_t *ssid)
{
    wifi_scanFilterMode_t   mode = WIFI_SCANFILTER_MODE_FIRST;

    WIFIHAL_TM_START();
    int ret = wifi_setApScanFilter(ssid->index, mode, NULL);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setApScanFilter(%d, %d)",
                                   ssid->index, mode);
    if (ret != RETURN_OK) {
        LOGW("%s: Failed to set scanfilter to %d", ssid->ifname, mode);
        return false;
    }

    LOGI("%s: scanfilter set to %u", ssid->ifname, mode);

    return true;
}

static bool
wifihal_discover_radios(void)
{
    wifihal_radio_t     *radio;
    ULONG               r, rnum;
    INT                 rcnt = 0;
    INT                 ret;

    WIFIHAL_TM_START();
    ret = wifi_getRadioNumberOfEntries(&rnum);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioNumberOfEntries() ret %ld",
                                       rnum);
    if (ret != RETURN_OK) {
        LOGE("Failed to get radio count");
        return false;
    }
    else if (rnum == 0) {
        LOGW("No radios detected");
        return false;
    }

    ds_dlist_init(&wifihal_radios, wifihal_radio_t, dsl_node);

    for (r = 0; r < rnum; r++)
    {
        if (!(radio = calloc(1, sizeof(*radio)))) {
            LOGEM("Failed to allocate memory for new radio");
            return false;
        }
        radio->index = (int)r;
        ds_dlist_init(&radio->ssids, wifihal_ssid_t, dsl_node);

        WIFIHAL_TM_START();
        ret = wifi_getRadioIfName(radio->index, radio->ifname);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioIfName(%d) ret \"%s\"",
                                  radio->index, (ret == RETURN_OK) ? radio->ifname : "");
        if (ret != RETURN_OK) {
            LOGE("Skipping radio #%d -- failed to get ifname", radio->index);
            free(radio);
            continue;
        }

        WIFIHAL_TM_START();
        ret = wifi_getRadioOperatingFrequencyBand(radio->index, radio->band);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioOperatingFrequencyBand(%d) ret \"%s\"",
                                                  radio->index, (ret == RETURN_OK) ? radio->band : "");
        if (ret != RETURN_OK) {
            LOGE("%s: Skipping radio #%d -- failed to get band", radio->ifname, radio->index);
            free(radio);
            continue;
        }

        rcnt++;
        LOGD("Found Radio Idx %d: %s (%s)", radio->index, radio->ifname, radio->band);

        radio->enabled = true;
        radio->channel_sync = false;

        ds_dlist_insert_tail(&wifihal_radios, radio);
    }

    LOGI("Discovered %d Wifi Radios", rcnt);

    if (rcnt == 0) {
        return false;
    }

    return true;
}

static bool
wifihal_discover_ssids(void)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ULONG               s, snum;
    bool                exists;
    INT                 ret;
    int                 radio_idx, scnt = 0;

    WIFIHAL_TM_START();
    ret = wifi_getSSIDNumberOfEntries(&snum);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDNumberOfEntries() ret %ld",
                                      snum);
    if (ret != RETURN_OK) {
        LOGE("Failed to get SSID count");
        return false;
    }
    else if (snum == 0) {
        LOGW("No SSIDs detected");
        return false;
    }

    for (s = 0; s < snum; s++)
    {
        if (!(ssid = calloc(1, sizeof(*ssid)))) {
            LOGEM("Failed to allocate memory for new SSID");
            return false;
        }
        ssid->index = (int)s;

        WIFIHAL_TM_START();
        ret = wifi_getApName(ssid->index, ssid->ifname);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getApName(%d) ret \"%s\"",
                             ssid->index, (ret == RETURN_OK) ? ssid->ifname : "");
        if (ret != RETURN_OK) {
            LOGE("Skipping ssid #%d -- failed to get ifname", ssid->index);
            free(ssid);
            continue;
        }

        // Filter SSID's that we don't have mappings for
        if (!target_unmap_ifname_exists(ssid->ifname))
        {
            LOGW("%s: Skipping VIF (no mapping exists)", ssid->ifname);
            free(ssid);
            continue;
        }

        WIFIHAL_TM_START();
        ret = wifi_getSSIDRadioIndex(ssid->index, &radio_idx);
        WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getSSIDRadioIndex(%d) ret %d",
                                     ssid->index, radio_idx);
        if (ret != RETURN_OK) {
            LOGE("%s: Skipping ssid -- failed to get radio index", ssid->ifname);
            free(ssid);
            continue;
        }

        if (!os_nif_exists(ssid->ifname, &exists) || exists == false)
        {
            // VAP doesn't exist -- silently skip it
            free(ssid);
            continue;
        }

        if (!(radio = wifihal_radio_by_index(radio_idx)))
        {
            LOGE("%s: Skipping ssid -- radio index %d not found", ssid->ifname, radio_idx);
            free(ssid);
            continue;
        }
        ssid->radio = radio;

        ds_tree_init(&ssid->keys,
                     (ds_key_cmp_t *)strcmp,
                     wifihal_key_t,
                     dst_node);

        scnt++;
        LOGD("%s: Found SSID Idx %d: %s", radio->ifname, ssid->index, ssid->ifname);

        ds_dlist_insert_tail(&radio->ssids, ssid);
    }

    LOGI("Discovered %d Wifi SSIDs", scnt);

    if (scnt == 0) {
        return false;
    }

    return true;
}

/*****************************************************************************/

bool
wifihal_init(struct ev_loop *loop, bool wifi)
{
    CHAR                str[WIFIHAL_MAX_BUFFER];
    INT                 ret;

    if (wifihal_evloop) {
        LOGW("wifihal_init called but already initialized!");
        return true;
    }
    wifihal_evloop = loop;

    WIFIHAL_TM_START();
    ret = wifi_getHalVersion(str);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getHalVersion() ret \"%s\"",
                             (ret == RETURN_OK) ? str : "");
    if (ret != RETURN_OK) {
        LOGE("wifihal_init failed to get HAL version");
        return false;
    }

    LOGN("wifi_hal v%s initializing", str);

    if (wifi)
    {
        if (wifihal_discover_radios() == false)
        {
            wifihal_cleanup();
            return false;
        }

        if (wifihal_discover_ssids() == false)
        {
            wifihal_cleanup();
            return false;
        }
    }

    return true;
}

bool
wifihal_cleanup(void)
{
    ds_dlist_iter_t     radio_iter;
    wifihal_radio_t     *radio;
    ds_dlist_iter_t     ssid_iter;
    wifihal_ssid_t      *ssid;

    wifihal_clients_cleanup();

    radio = ds_dlist_ifirst(&radio_iter, &wifihal_radios);
    while (radio)
    {
        ssid = ds_dlist_ifirst(&ssid_iter, &radio->ssids);
        while (ssid)
        {
            ds_dlist_iremove(&ssid_iter);
            wifihal_security_key_cleanup(ssid);
            evsched_task_cancel_by_find(NULL, ssid, EVSCHED_FIND_BY_ARG);
            free(ssid);

            ssid = ds_dlist_inext(&ssid_iter);
        }

        ds_dlist_iremove(&radio_iter);
        evsched_task_cancel_by_find(NULL, radio, EVSCHED_FIND_BY_ARG);
        free(radio);

        radio = ds_dlist_inext(&radio_iter);
    }

    wifihal_evloop = NULL;

    return true;
}

bool
wifihal_config_init(void)
{
    wifihal_radio_t         *radio;
    wifihal_ssid_t          *ssid;
#ifdef QTN_WIFI
    char                    buf[64];
    INT                     ret;
#endif /* QTN_WIFI */

    // Apply initial configuration
    ds_dlist_foreach(&wifihal_radios, radio)
    {
#ifdef QTN_WIFI
        // QTN: If in 160MHz width on 5G, change it to 80MHz until cloud requests 160MHz
        if (radio->band[0] == '5')
        {
            *buf = '\0';
            WIFIHAL_TM_START();
            ret = wifi_getRadioOperatingChannelBandwidth(radio->index, buf);
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioOperatingChannelBandwidth(%d) ret \"%s\"",
                                                         radio->index, buf);
            if (ret == RETURN_OK && !strncmp(buf, "160", 3))
            {
                strcpy(buf, "80MHz");
                WIFIHAL_TM_START();
                ret = wifi_setRadioOperatingChannelBandwidth(radio->index, buf);
                WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_setRadioOperatingChannelBandwidth(%d, \"%s\")",
                                                             radio->index, buf);
                LOGD("[WIFI_HAL SET] wifi_setRadioOperatingChannelBandwidth(%d, \"%s\") = %d",
                                                             radio->index, buf, ret);
            }
        }
#endif /* QTN_WIFI */

        ds_dlist_foreach(&radio->ssids, ssid)
        {
            // Get current enabled state
            ssid->enabled = wifihal_vif_is_enabled(ssid);

            // Configure SSID
            wifihal_csa_deauth_set(ssid);
            wifihal_scanfilter_set(ssid);

#ifdef QTN_WIFI
            // QTN WAR: - clear initial acl list for backhaul ssid
            char *unmapped_ifname = target_unmap_ifname(ssid->ifname);
            if ((!strncmp(unmapped_ifname, "bhaul-ap-", 9)))
            {
                WIFIHAL_TM_START();
                ret = wifi_delApAclDevices(ssid->index);
                WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_delApAclDevices(%d)",
                                           ssid->index);
                LOGD("[WIFI_HAL SET] wifi_delApAclDevices(%d) = %d",
                                           ssid->index, ret);
            }
#endif /* QTN_WIFI */
        }
    }

    // Initialize health check
    if (!wifihal_health_init())
    {
        return false;
    }

    // Initialize cloud mode
    return wifihal_cloud_mode_init();
}

bool
wifihal_config_cleanup(void)
{
    wifihal_health_cleanup();

    return wifihal_sync_cleanup();
}

ds_dlist_t *
wifihal_get_radios(void)
{
    return &wifihal_radios;
}

wifihal_radio_t *
wifihal_radio_by_index(int index)
{
    wifihal_radio_t         *radio;

    ds_dlist_foreach(&wifihal_radios, radio) {
        if (radio->index == index) {
            return radio;
        }
    }

    return NULL;
}

wifihal_radio_t *
wifihal_radio_by_ifname(const char *ifname)
{
    wifihal_radio_t         *radio;

    ds_dlist_foreach(&wifihal_radios, radio) {
        if (!strcmp(radio->ifname, ifname)) {
            return radio;
        }
    }

    return NULL;
}

wifihal_ssid_t *
wifihal_ssid_by_index(int index)
{
    wifihal_radio_t         *radio;
    wifihal_ssid_t          *ssid;

    ds_dlist_foreach(&wifihal_radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            if (ssid->index == index) {
                return ssid;
            }
        }
    }

    return NULL;
}

wifihal_ssid_t *
wifihal_ssid_by_ifname(const char *ifname)
{
    wifihal_radio_t         *radio;
    wifihal_ssid_t          *ssid;

    ds_dlist_foreach(&wifihal_radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            if (!strcmp(ssid->ifname, ifname)) {
                return ssid;
            }
        }
    }

    return NULL;
}

bool
wifihal_redirector_update(const char *addr)
{
    struct schema_AWLAN_Node    awlan;
    schema_filter_t             filter;

    if (!wifihal_devconf_cb) {
        return false;
    }

    LOGN("### Setting redirector to '%s' ###", addr);

    memset(&awlan, 0, sizeof(awlan));
    schema_filter_init(&filter, "+");

    strncpy(awlan.redirector_addr, addr, sizeof(awlan.redirector_addr)-1);
    schema_filter_add(&filter, "redirector_addr");

    wifihal_devconf_cb(&awlan, &filter);

    return true;
}

bool
wifihal_device_config_init(void)
{
    char        buf[REDIR_ADDR_MAX];
    char        *p;

    if (!wifihal_sync_connected()) {
        LOGT("wifihal_device_config_init: Sync not connected, delaying init...");
        return true;
    }

    if (devinfo_getv(DEVINFO_MESH_URL, ARRAY_AND_SIZE(buf), false))
    {
        p = buf;
    }
    else
    {
        LOGW("### Failed to load mesh backhaul, using default ###");
        p = CONTROLLER_ADDR;
    }

    return wifihal_redirector_update(p);
}

bool
wifihal_device_config_register(void *devconf_cb)
{

    wifihal_devconf_cb = devconf_cb;

    return wifihal_device_config_init();
}
