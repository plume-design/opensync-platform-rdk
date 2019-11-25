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

#include "evsched.h"
#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "const.h"

#include "target.h"
#include "target_internal.h"

#include "osync_hal.h"

#define MODULE_ID LOG_MODULE_ID_OSA

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

typedef enum
{
    OSYNC_BAND_2G,
    OSYNC_BAND_5G,
    OSYNC_BAND_UNKNOWN
} osync_band_t;

typedef enum
{
    OSYNC_BACKHAUL_IFACE_TYPE,
    OSYNC_OTHER_IFACE_TYPE,
    OSYNC_UNKNOWN_VAP_TYPE
} osync_vap_type_t;

typedef void (*devconf_cb_t)(
        struct schema_AWLAN_Node *awlan,
        schema_filter_t *filter);

struct ev_loop *wifihal_evloop = NULL;

/******************************************************************************
 *  TARGET definitions
 *****************************************************************************/

bool target_ready(struct ev_loop *loop)
{
    char        tmp[64];

    if (osync_hal_ready() != OSYNC_HAL_SUCCESS)
    {
        LOGW("Target not ready, OSync HAL not ready");
        return false;
    }

    // Check if we can read important entity information

    if (!target_serial_get(ARRAY_AND_SIZE(tmp)))
    {
        LOGW("Target not ready, failed to query serial number");
        return false;
    }

    if (!target_id_get(ARRAY_AND_SIZE(tmp)))
    {
        LOGW("Target not ready, failed to query id (CM MAC)");
        return false;
    }

    if (!target_model_get(ARRAY_AND_SIZE(tmp)))
    {
        LOGW("Target not ready, failed to query model number");
        return false;
    }

    if (!target_platform_version_get(ARRAY_AND_SIZE(tmp)))
    {
        LOGW("Target not ready, failed to query platform version");
        return false;
    }

    wifihal_evloop = loop;

    return true;
}

static osync_band_t vap_band(INT ssid_index)
{
    INT ret;
    INT radio_index;
    char band[64];  // Unfortunately there is no define for band length in wifi_hal.h
                    // However, according to comment in that file, the maximum
                    // length is 64 bytes.

    ret = wifi_getSSIDRadioIndex(ssid_index, &radio_index);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio index for SSID %d\n", __func__, ssid_index);
        return OSYNC_BAND_UNKNOWN;
    }

    memset(band, 0, sizeof(band));
    ret = wifi_getRadioOperatingFrequencyBand(radio_index, band);

    if (band[0] == '2')
    {
        return OSYNC_BAND_2G;
    }
    if (band[0] == '5')
    {
        return OSYNC_BAND_5G;
    }

    return OSYNC_BAND_UNKNOWN;
}

static osync_vap_type_t vap_type(const char *ssid)
{
    char *unmapped_ifname;

    unmapped_ifname = target_unmap_ifname((char *)ssid);
    if (!strncmp(unmapped_ifname, "bhaul-", 6))
    {
        return OSYNC_BACKHAUL_IFACE_TYPE;
    }

    return OSYNC_OTHER_IFACE_TYPE;
}

static bool set_csa_deauth(INT ssid_index, const char *ssid)
{
    INT ret;
    uint32_t deauth_flags = CSA_DEAUTH_DEFAULT;

    // We only set flags other than default to 2.4GHz non-backhaul VAPs
    if ((vap_type(ssid) == OSYNC_OTHER_IFACE_TYPE) &&
            vap_band(ssid_index) == OSYNC_BAND_2G)
    {
        deauth_flags = CSA_2G_OTHER_DEAUTH;
    }

    ret = wifi_setApCsaDeauth(ssid_index, deauth_flags);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot set AP CSA Deauth flags %d for index %d\n", __func__,
              deauth_flags, ssid_index);
        return false;
    }

    return true;
}

static bool set_deauth_and_scan_filter_flags()
{
    INT ret;
    ULONG snum;
    ULONG i;
    char ssid[64];  // According to wifi_hal.h header the SSID is up to 16 bytes.
                    // Allocate more just in case.

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        return false;
    }

    if (snum == 0)
    {
        LOGE("%s: no SSIDs detected", __func__);
        return false;
    }

    for (i = 0; i < snum; i++)
    {
        memset(ssid, 0, sizeof(ssid));
        ret = wifi_getApName(i, ssid);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get AP name for index %lu. Skipping.\n", __func__, i);
            continue;
        }

        // Filter SSID's that we don't have mappings for
        if (!target_unmap_ifname_exists(ssid))
        {
            LOGI("%s: Skipping VIF (no mapping exists)", ssid);
            continue;
        }
        LOGI("Found SSID index %lu: %s", i, ssid);

        if (!set_csa_deauth(i, ssid))
        {
            LOGE("Cannot set CSA for %s", ssid);
            return false;
        }

        ret = wifi_setApScanFilter(i, WIFI_SCANFILTER_MODE_FIRST, NULL);
        if (ret != RETURN_OK)
        {
            LOGE("%s: Failed to set scanfilter to WIFI_SCANFILTER_MODE_FIRST", ssid);
            return false;
        }
        LOGI("%s: scanfilter set to WIFI_SCANFILTER_MODE_FIRST", ssid);
    }

    return true;
}

bool target_init(target_init_opt_t opt, struct ev_loop *loop)
{
    INT ret;
    CHAR str[64];  // Unfortunately, the RDK Wifi HAL doesn't specify
                   // the maximum length of version string. It is usually
                   // something like "2.0.0", so assume 64 is enough.

    if (!target_map_ifname_init())
    {
        LOGE("Target init failed to initialize interface mapping");
        return false;
    }

    if (osync_hal_init() != OSYNC_HAL_SUCCESS)
    {
        LOGE("OSync HAL init failed");
        return false;
    }

    wifihal_evloop = loop;

    ret = wifi_getHalVersion(str);
    if (ret != RETURN_OK)
    {
        LOGE("Manager %d: cannot get HAL version", opt);
        return false;
    }

    switch (opt)
    {
        case TARGET_INIT_MGR_SM:
            break;

        case TARGET_INIT_MGR_WM:
            if (evsched_init(loop) == false)
            {
                LOGE("Initializing WM "
                        "(Failed to initialize EVSCHED)");
                return -1;
            }

            if (!sync_init(SYNC_MGR_WM))
            {
                // It reports the error
                return false;
            }
            if (!set_deauth_and_scan_filter_flags())
            {
                LOGE("Failed to set csa_deauth and scan filter flags");
                return false;
            }

            break;

        case TARGET_INIT_MGR_CM:
            if (!sync_init(SYNC_MGR_CM))
            {
                // It reports the error
                return false;
            }
            break;

        case TARGET_INIT_MGR_BM:
            break;

        default:
            break;
    }

    return true;
}

bool target_close(target_init_opt_t opt, struct ev_loop *loop)
{
    if (osync_hal_deinit() != OSYNC_HAL_SUCCESS)
    {
        LOGW("OSync HAL deinit failed.");
    }

    switch (opt)
    {
        case TARGET_INIT_MGR_WM:
            sync_cleanup();
            /* fall through */

        case TARGET_INIT_MGR_SM:
            break;

        default:
            break;
    }

    target_map_close();

    return true;
}

const char* target_persistent_storage_dir(void)
{
    return TARGET_PERSISTENT_STORAGE;
}

const char* target_scripts_dir(void)
{
    return TARGET_SCRIPTS_PATH;
}

const char* target_tools_dir(void)
{
    return TARGET_TOOLS_PATH;
}

const char* target_bin_dir(void)
{
    return TARGET_BIN_PATH;
}

const char* target_speedtest_dir(void)
{
    return target_tools_dir();
}

/******************************************************************************
 *  SERVICE definitions
 *****************************************************************************/

static bool update_device_mode(devconf_cb_t devconf_cb)
{
    struct schema_AWLAN_Node awlan;
    schema_filter_t filter;
    const char *device_mode_value;

    memset(&awlan, 0, sizeof(awlan));
    schema_filter_init(&filter, "+");

    osync_hal_devinfo_cloud_mode_t mode = OSYNC_HAL_DEVINFO_CLOUD_MODE_UNKNOWN;

    if (osync_hal_devinfo_get_cloud_mode(&mode) != OSYNC_HAL_SUCCESS)
    {
        LOGE("Cannot get cloud mode");
        return false;
    }

    if (mode == OSYNC_HAL_DEVINFO_CLOUD_MODE_UNKNOWN)
    {
        LOGW("Unknown cloud mode");
        return false;
    }

    switch (mode)
    {
        case OSYNC_HAL_DEVINFO_CLOUD_MODE_FULL:
            device_mode_value = SCHEMA_CONSTS_DEVICE_MODE_CLOUD;
            break;
        case OSYNC_HAL_DEVINFO_CLOUD_MODE_MONITOR:
            device_mode_value = SCHEMA_CONSTS_DEVICE_MODE_MONITOR;
            break;
        default:
            LOGW("Failed to set device mode! :: unknown value = %d", mode);
            return false;
    }

    LOGN("### Setting device mode to '%s' mode ###", device_mode_value);
    SCHEMA_FF_SET_STR(&filter, &awlan, device_mode, device_mode_value);
    devconf_cb(&awlan, &filter);

    return true;
}

static bool update_redirector_addr(devconf_cb_t devconf_cb)
{
    struct schema_AWLAN_Node awlan;
    schema_filter_t filter;
    char buf[64];

    memset(buf, 0, sizeof(buf));
    memset(&awlan, 0, sizeof(awlan));
    schema_filter_init(&filter, "+");

    if (osync_hal_devinfo_get_redirector_addr(buf, sizeof(buf)) != OSYNC_HAL_SUCCESS)
    {
        LOGW("### Failed to get redirector address, using default ###");
        memset(buf, 0, sizeof(buf));
        STRSCPY(buf, CONTROLLER_ADDR);
    }

    STRSCPY(awlan.redirector_addr, buf);
    schema_filter_add(&filter, "redirector_addr");

    devconf_cb(&awlan, &filter);

    return true;
}

bool target_device_config_register(void *devconf_cb)
{
    if (!update_device_mode(devconf_cb))
    {
        return false;
    }

    return update_redirector_addr(devconf_cb);
}

void
target_fatal_restart(bool block, char *reason)
{
    const char      *scripts_dir = target_scripts_dir();
    char            cmd[256];
    int             max_fd, fd;

    LOGEM("===== Fatal Restart Triggered: %s =====", reason ? reason : "???");

    // Build our restart path
    snprintf(cmd, sizeof(cmd)-1, "%s/restart.sh", scripts_dir);

    // Fork to run restart command
    if (fork() != 0)
    {
        if (block)
        {
            while(1);
        }
        return;
    }
    setsid();

    // Close sockets from 3 and above
    max_fd = sysconf(_SC_OPEN_MAX);
    for (fd = 3; fd < max_fd; fd++)
    {
        close(fd);
    }

    // Sleep a few seconds before restart
    sleep(3);

    execl(cmd, cmd, NULL);
    exit(1);
}

