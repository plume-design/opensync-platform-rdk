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

#include "os.h"
#include "os_nif.h"
#include "log.h"
#include "const.h"

#include "target.h"
#include "wifihal.h"


#define MODULE_ID LOG_MODULE_ID_OSA


/******************************************************************************
 *  TARGET definitions
 *****************************************************************************/

bool target_ready(struct ev_loop *loop)
{
    char        tmp[64];

    if (!os_nif_is_interface_ready(BACKHAUL_IFNAME_2G)) {
        LOGW("Target not ready, '%s' is not UP", BACKHAUL_IFNAME_2G);
        return false;
    }

    if (!os_nif_is_interface_ready(BACKHAUL_IFNAME_5G)) {
        LOGW("Target not ready, '%s' is not UP", BACKHAUL_IFNAME_5G);
        return false;
    }

    // Check important entity information (also causes it to be cached)

    if (!target_serial_get(ARRAY_AND_SIZE(tmp))) {
        LOGW("Target not ready, failed to query serial number");
        return false;
    }

    if (!target_id_get(ARRAY_AND_SIZE(tmp))) {
        LOGW("Target not ready, failed to query id (CM MAC)");
        return false;
    }

    if (!target_model_get(ARRAY_AND_SIZE(tmp))) {
        LOGW("Target not ready, failed to query model number");
        return false;
    }

    if (!target_platform_version_get(ARRAY_AND_SIZE(tmp))) {
        LOGW("Target not ready, failed to query platform version");
        return false;
    }

    return true;
}

bool target_vif_inet_init(void);
bool target_init(target_init_opt_t opt, struct ev_loop *loop)
{
    if (!target_map_ifname_init()) {
        LOGE("Target init failed to initialize interface mapping");
        return false;
    }

    switch (opt)
    {
    case TARGET_INIT_MGR_SM:
        /*
         * Due to logging verbosity, it was requested that we "tone down"
         * the messages coming from SM into RDK Logger.  Since we cannot set
         * separate severities per logger type, we'll just tone it all down
         * for now
         */
        log_module_severity_set(LOG_MODULE_ID_MAIN, LOG_SEVERITY_ERR);
        log_module_severity_set(LOG_MODULE_ID_IOCTL, LOG_SEVERITY_ERR);

        if (!wifihal_init(loop, true)) {
            // It reports the error
            return false;
        }
        break;

    case TARGET_INIT_MGR_WM:
        // Change default log severity level
        log_severity_set(LOG_SEVERITY_CRIT);

        // Change severity of OVSDB module; it's extremely verbose
        log_module_severity_set(LOG_MODULE_ID_OVSDB, LOG_SEVERITY_ERR);

        if (!wifihal_init(loop, true)) {
            // It reports the error
            return false;
        }
        if (!wifihal_sync_init(WIFIHAL_SYNC_MGR_WM)) {
            // It reports the error
            return false;
        }
        if (!wifihal_config_init()) {
            // It reports the error
            return false;
        }
        break;

    case TARGET_INIT_MGR_NM:
        // Change severity of OVSDB module; it's extremely verbose
        log_module_severity_set(LOG_MODULE_ID_OVSDB, LOG_SEVERITY_ERR);

        if (!target_vif_inet_init()) {
            // It reports the error
            return false;
        }
        if (!wifihal_init(loop, false)) {
            // It reports the error
            return false;
        }
        if (!wifihal_sync_init(WIFIHAL_SYNC_MGR_NM)) {
            // It reports the error
            return false;
        }
        break;

    case TARGET_INIT_MGR_CM:
        if (!wifihal_init(loop, false)) {
            // It reports the error
            return false;
        }
        if (!wifihal_sync_init(WIFIHAL_SYNC_MGR_CM)) {
            // It reports the error
            return false;
        }
        break;

    case TARGET_INIT_MGR_BM:
        if (!wifihal_init(loop, true)) {
            // It reports the error
            return false;
        }
        break;

    default:
        break;
    }

    return true;
}

bool target_close(target_init_opt_t opt, struct ev_loop *loop)
{
    switch (opt)
    {
    case TARGET_INIT_MGR_WM:
        wifihal_config_cleanup();
        /* fall through */

    case TARGET_INIT_MGR_SM:
        wifihal_cleanup();
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

bool target_master_state_register(const char *ifname, target_master_state_cb_t *mstate_cb)
{
    return false;
}

bool target_eth_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return false;
}

bool target_vif_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return true;
}

bool target_gre_master_state_get(const char *ifname, const char *remote_ip, struct schema_Wifi_Master_State *mstate)
{
    return false;
}

bool target_bridge_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate)
{
    return false;
}


/******************************************************************************
 *  SERVICE definitions
 *****************************************************************************/

bool target_device_config_register(void *devconf_cb)
{
    return wifihal_device_config_register(devconf_cb);
}
