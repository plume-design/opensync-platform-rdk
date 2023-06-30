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

#include "target.h"
#include "target_internal.h"
#include "log.h"
#include "dmcli.h"
#include "kconfig.h"

typedef void (*devconf_cb_t)(
        struct schema_AWLAN_Node *awlan,
        schema_filter_t *filter);

static devconf_cb_t awlan_cb = NULL;

void cloud_config_mode_init(void)
{
    char buf[128];
    radio_cloud_mode_t mode;

    memset(buf, 0, sizeof(buf));

    if (!dmcli_eRT_getv(DMCLI_ERT_MESH_STATE, ARRAY_AND_SIZE(buf), false))
    {
        LOGE("Failed to initialize cloud mode");
        return;
    }
    mode = RADIO_CLOUD_MODE_MONITOR;
    if (!strncasecmp(buf, "Full", sizeof(buf)))
    {
        mode = RADIO_CLOUD_MODE_FULL;
    }
    radio_cloud_mode_set(mode);
}

void cloud_config_set_mode(const char *device_mode)
{
    struct schema_AWLAN_Node awlan;
    schema_filter_t filter;

    memset(&awlan, 0, sizeof(awlan));
    schema_filter_init(&filter, "+");

    LOGN("### Setting device mode to '%s' mode ###", device_mode);
    SCHEMA_FF_SET_STR(&filter, &awlan, device_mode, device_mode);

    if (awlan_cb)
    {
        awlan_cb(&awlan, &filter);
    }
}

static bool update_redirector_addr(void)
{
    struct schema_AWLAN_Node awlan;
    schema_filter_t filter;
    char buf[64];

    memset(buf, 0, sizeof(buf));
    memset(&awlan, 0, sizeof(awlan));
    schema_filter_init(&filter, "+");

    if (!dmcli_eRT_getv(DMCLI_ERT_MESH_URL, ARRAY_AND_SIZE(buf), false))
    {
        LOGW("### Failed to get redirector address, using default ###");
        memset(buf, 0, sizeof(buf));
        STRSCPY(buf, CONTROLLER_ADDR);
    }

    STRSCPY(awlan.redirector_addr, buf);
    schema_filter_add(&filter, "redirector_addr");

    awlan_cb(&awlan, &filter);

    LOGI("Redirector is changed to %s", buf);

    return true;
}

bool target_device_config_register(void *devconf_cb)
{
    awlan_cb = devconf_cb;
    char buf[128];

    memset(buf, 0, sizeof(buf));

    if (kconfig_enabled(CONFIG_RDK_EXTENDER))
    {
        cloud_config_set_mode(SCHEMA_CONSTS_DEVICE_MODE_CLOUD);
    }
    else
    {
        if (dmcli_eRT_getv(DMCLI_ERT_MESH_STATE, ARRAY_AND_SIZE(buf), false))
        {
            if (!strncasecmp(buf, "Full", sizeof(buf)))
            {
                cloud_config_set_mode(SCHEMA_CONSTS_DEVICE_MODE_CLOUD);
            }
            else
            {
                cloud_config_set_mode(SCHEMA_CONSTS_DEVICE_MODE_MONITOR);
            }
        }
    }
    return update_redirector_addr();
}
