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
 * wifihal_health.c
 *
 * RDKB Platform - Wifi HAL - Health Check
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

#include "evsched.h"
#include "log.h"
#include "target.h"
#include "wifihal.h"

/*****************************************************************************/

#define MODULE_ID           LOG_MODULE_ID_HAL

#define HEALTH_CHECK_IVAL   10

/*****************************************************************************/

static void
wifihal_health_check_ssids(void)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ds_dlist_t          *radios = wifihal_get_radios();
    bool                enabled;
    char                reason[64];

    ds_dlist_foreach(radios, radio)
    {
        ds_dlist_foreach(&radio->ssids, ssid)
        {
            // Ensure SSID enabled state is correct
            enabled = wifihal_vif_is_enabled(ssid);
            if (enabled != ssid->enabled)
            {
                LOGE("%s: Health check -- Enabled mismatch (%d != %d)",
                     ssid->ifname, enabled, ssid->enabled);
                snprintf(reason, sizeof(reason)-1, "%s enabled mismatch", ssid->ifname);
                wifihal_fatal_restart(true, reason);
            }
        }
    }

    return;
}

static void
wifihal_health_check_channels(void)
{
    wifihal_radio_t     *radio;
    ds_dlist_t          *radios = wifihal_get_radios();
    ULONG               chan;
    INT                 ret;

    ds_dlist_foreach(radios, radio)
    {
        if (!radio->csa_in_progress)
        {
            WIFIHAL_TM_START();
            ret = wifi_getRadioChannel(radio->index, &chan);
            WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioChannel(%d) ret %ld",
                                       radio->index, chan);
            if (ret == RETURN_OK)
            {
                if ((int)chan != radio->channel)
                {
                    LOGW("%s: Health check -- channel mismatch (%ld != %d), updating...",
                         radio->ifname, chan, radio->channel);
                    wifihal_radio_state_updated(radio);
                }
            }
        }
    }

    return;
}

static void
wifihal_health_check(void *arg)
{
    // Perform health checks
    wifihal_health_check_ssids();
    wifihal_health_check_channels();

    // Reschedule health check
    evsched_task_reschedule_ms(EVSCHED_SEC(HEALTH_CHECK_IVAL));

    return;
}

/*****************************************************************************/

bool
wifihal_health_init(void)
{
    LOGI("Starting health check (%ds interval)...", HEALTH_CHECK_IVAL);
    evsched_task(&wifihal_health_check, NULL, EVSCHED_SEC(HEALTH_CHECK_IVAL));

    return true;
}

bool
wifihal_health_cleanup(void)
{
    LOGI("Stopping health check...");
    evsched_task_cancel_by_find(&wifihal_health_check, NULL, EVSCHED_FIND_BY_FUNC);

    return true;
}

void
wifihal_fatal_restart(bool block, char *reason)
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
        if (block) {
            while(1);
        }
        return;
    }
    setsid();

    // Close sockets from 3 and above
    max_fd = sysconf(_SC_OPEN_MAX);
    for (fd = 3; fd < max_fd; fd++) {
        close(fd);
    }

    // Sleep a few seconds before restart
    sleep(3);

    execl(cmd, cmd, NULL);
    exit(1);
}
