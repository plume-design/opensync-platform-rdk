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
#include "target.h"
#include "devinfo.h"
#include "const.h"

/*****************************************************************************/

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
} target_entity_cache;


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

bool target_serial_get(void *buff, size_t buffsz)
{
    if (!target_entity_cache.serial_cached)
    {
        if (!devinfo_getv(DEVINFO_SERIAL_NUM,
                          ARRAY_AND_SIZE(target_entity_cache.serial),
                          false))
        {
            return false;
        }
        target_entity_cache.serial_cached = true;
    }

    snprintf(buff, buffsz, "%s", target_entity_cache.serial);
    return true;
}

bool target_id_get(void *buff, size_t buffsz)
{
    if (!target_entity_cache.id_cached)
    {
        if (!devinfo_getv(DEVINFO_CM_MAC,
                          ARRAY_AND_SIZE(target_entity_cache.id),
                          false))
        {
            return false;
        }

        if (strlen(target_entity_cache.id) != 17)
        {
            LOGE("target_id_get() bad CM_MAC format");
            return false;
        }

        target_entity_cache.id_cached = true;
    }

    snprintf(buff,
             buffsz,
             "%c%c%c%c%c%c%c%c%c%c%c%c",
             toupper(target_entity_cache.id[0]),
             toupper(target_entity_cache.id[1]),
             // target_entity_cache.id[2] == ":"
             toupper(target_entity_cache.id[3]),
             toupper(target_entity_cache.id[4]),
             // target_entity_cache.id[5] == ":"
             toupper(target_entity_cache.id[6]),
             toupper(target_entity_cache.id[7]),
             // target_entity_cache.id[8] == ":"
             toupper(target_entity_cache.id[9]),
             toupper(target_entity_cache.id[10]),
             // target_entity_cache.id[11] == ":"
             toupper(target_entity_cache.id[12]),
             toupper(target_entity_cache.id[13]),
             // target_entity_cache.id[14] == ":"
             toupper(target_entity_cache.id[15]),
             toupper(target_entity_cache.id[16]));

    return true;
}

bool target_sku_get(void *buff, size_t buffsz)
{
    // SKU info not available
    return false;
}

bool target_model_get(void *buff, size_t buffsz)
{
    if (!target_entity_cache.model_cached)
    {
        if (!devinfo_getv(DEVINFO_MODEL_NUM,
                          ARRAY_AND_SIZE(target_entity_cache.model),
                          false))
        {
            return false;
        }
        target_entity_cache.model_cached = true;
    }

    snprintf(buff, buffsz, "%s", target_entity_cache.model);
    return true;
}

bool target_sw_version_get(void *buff, size_t buffsz)
{
    snprintf(buff, buffsz, "%s", app_build_ver_get());
    return true;
}

bool target_hw_revision_get(void *buff, size_t buffsz)
{
    // HW version info not available
    return false;
}

bool target_platform_version_get(void *buff, size_t buffsz)
{
    if (!target_entity_cache.pver_cached)
    {
        if (!devinfo_getv(DEVINFO_SOFTWARE_VER,
                          ARRAY_AND_SIZE(target_entity_cache.pver),
                          false))
        {
            return false;
        }
        target_entity_cache.pver_cached = true;
    }

    snprintf(buff, buffsz, "%s", target_entity_cache.pver);
    return true;
}
