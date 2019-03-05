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
 * wifihal_cloud_mode.c
 *
 * RDKB Platform - Wifi HAL - Cloud Mode Handling
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
#include "devinfo.h"
#include "wifihal.h"
#include "target.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID   LOG_MODULE_ID_HAL

/*****************************************************************************/

static wifihal_cloud_mode_t wifihal_cloud_mode = WIFIHAL_CLOUD_MODE_UNKNOWN;

/*****************************************************************************/

bool
wifihal_cloud_mode_init(void)
{
    wifihal_cloud_mode_t    mode = WIFIHAL_CLOUD_MODE_MONITOR;
    char                    buf[128];

    if (devinfo_getv(DEVINFO_MESH_STATE, ARRAY_AND_SIZE(buf), false))
    {
        if (!strncmp(buf, "Full", 4) || !strncmp(buf, "full", 4))
        {
            mode = WIFIHAL_CLOUD_MODE_FULL;
        }
    }

    wifihal_cloud_mode = mode;

    return true;
}

bool
wifihal_cloud_mode_sync(void)
{
    return wifihal_sync_send_status(wifihal_cloud_mode);
}

bool
wifihal_cloud_mode_set(wifihal_cloud_mode_t mode)
{
    wifihal_cloud_mode = mode;

    return wifihal_cloud_mode_sync();
}

wifihal_cloud_mode_t
wifihal_cloud_mode_get(void)
{
    return wifihal_cloud_mode;
}

wifihal_chan_mode_t
wifihal_cloud_mode_get_chan_mode(wifihal_radio_t *radio)
{
    if (wifihal_cloud_mode_get() == WIFIHAL_CLOUD_MODE_FULL)
    {
        return WIFIHAL_CHAN_MODE_CLOUD;
    }

    if (radio->auto_chan)
    {
        return WIFIHAL_CHAN_MODE_AUTO;
    }

    return WIFIHAL_CHAN_MODE_MANUAL;
}
