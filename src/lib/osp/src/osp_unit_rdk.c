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
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#include "log.h"
#include "dmcli.h"
#include "const.h"
#include "build_version.h"
#include "kconfig.h"
#include "util.h"

#include "osp_unit.h"

#define MODULE_ID LOG_MODULE_ID_OSA

#define MAX_CACHE_LEN       64

/*****************************************************************************/

static struct
{
    bool        serial_cached;
    char        serial[MAX_CACHE_LEN];

    bool        id_cached;
    char        id[MAX_CACHE_LEN];

    bool        model_cached;
    char        model[MAX_CACHE_LEN];

    bool        pver_cached;
    char        pver[MAX_CACHE_LEN];
} osp_unit_cache;

bool osp_unit_serial_get(char *buff, size_t buffsz)
{
    if (!osp_unit_cache.serial_cached)
    {
        if (!dmcli_eRT_getv(DMCLI_ERT_SERIAL_NUM,
                          ARRAY_AND_SIZE(osp_unit_cache.serial), false))
        {
            return false;
        }
        osp_unit_cache.serial_cached = true;
    }

    snprintf(buff, buffsz, "%s", osp_unit_cache.serial);
    return true;
}

bool osp_unit_id_get(char *buff, size_t buffsz)
{
    if (!osp_unit_cache.id_cached)
    {
        if (!dmcli_eRT_getv(DMCLI_ERT_CM_MAC,
                          ARRAY_AND_SIZE(osp_unit_cache.id), false))
        {
            return false;
        }

        if (strlen(osp_unit_cache.id) != 17)
        {
            LOGE("osp_unit_id_get() bad CM_MAC format");
            return false;
        }

        osp_unit_cache.id_cached = true;
    }

    snprintf(buff,
             buffsz,
             "%c%c%c%c%c%c%c%c%c%c%c%c",
             toupper(osp_unit_cache.id[0]),
             toupper(osp_unit_cache.id[1]),
             // osp_unit_cache.id[2] == ":"
             toupper(osp_unit_cache.id[3]),
             toupper(osp_unit_cache.id[4]),
             // osp_unit_cache.id[5] == ":"
             toupper(osp_unit_cache.id[6]),
             toupper(osp_unit_cache.id[7]),
             // osp_unit_cache.id[8] == ":"
             toupper(osp_unit_cache.id[9]),
             toupper(osp_unit_cache.id[10]),
             // osp_unit_cache.id[11] == ":"
             toupper(osp_unit_cache.id[12]),
             toupper(osp_unit_cache.id[13]),
             // osp_unit_cache.id[14] == ":"
             toupper(osp_unit_cache.id[15]),
             toupper(osp_unit_cache.id[16]));

    return true;
}

bool osp_unit_sku_get(char *buff, size_t buffsz)
{
    // SKU info not available
    return false;
}

bool osp_unit_model_get(char *buff, size_t buffsz)
{
    if (!osp_unit_cache.model_cached)
    {
        if (!dmcli_eRT_getv(DMCLI_ERT_MODEL_NUM,
                          ARRAY_AND_SIZE(osp_unit_cache.model), false))
        {
            return false;
        }
        osp_unit_cache.model_cached = true;
    }

    snprintf(buff, buffsz, "%s", osp_unit_cache.model);
    return true;
}

bool osp_unit_sw_version_get(char *buff, size_t buffsz)
{
    snprintf(buff, buffsz, "%s", app_build_ver_get());
    return true;
}

bool osp_unit_hw_revision_get(char *buff, size_t buffsz)
{
    // HW version info not available
    return false;
}

bool osp_unit_platform_version_get(char *buff, size_t buffsz)
{
    if (!osp_unit_cache.pver_cached)
    {
        if (!dmcli_eRT_getv(DMCLI_ERT_SOFTWARE_VER,
                          ARRAY_AND_SIZE(osp_unit_cache.pver), false))
        {
            return false;
        }
        osp_unit_cache.pver_cached = true;
    }

    snprintf(buff, buffsz, "%s", osp_unit_cache.pver);
    return true;
}

bool osp_unit_vendor_part_get(char *buff, size_t buffsz)
{
    return false;
}

bool osp_unit_manufacturer_get(char *buff, size_t buffsz)
{
    return false;
}

bool osp_unit_factory_get(char *buff, size_t buffsz)
{
    return false;
}

bool osp_unit_mfg_date_get(char *buff, size_t buffsz)
{
    return false;
}

bool osp_unit_vendor_name_get(char *buff, size_t buffsz)
{
    return false;
}

bool osp_unit_dhcpc_hostname_get(void *buff, size_t buffsz)
{
    char serial_num[buffsz];
    char model_name[buffsz];

    memset(serial_num, 0, (sizeof(char) * buffsz));
    memset(model_name, 0, (sizeof(char) * buffsz));

    if (!osp_unit_serial_get(serial_num, sizeof(serial_num)))
    {
        LOG(ERR, "Unable to get serial number");
        return false;
    }
    if (!osp_unit_model_get(model_name, sizeof(model_name)))
    {
        LOG(ERR, "Unable to get model name");
        return false;
    }

    snprintf(buff, buffsz, "%s_%s", serial_num, model_name);

    return true;
}
