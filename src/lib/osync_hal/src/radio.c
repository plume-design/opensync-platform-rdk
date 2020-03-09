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

#include <ctype.h>

#include "osync_hal.h"
#include "target.h"
#include "target_internal.h"

#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_GET_COUNTRY_CODE

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

static c_item_t g_map_country_str[] =
{
    C_ITEM_STR_STR("826", "UK"),  // ISO 3166-1
    C_ITEM_STR_STR("840", "US"),
    C_ITEM_STR_STR("841", "US"),  // (non-standard)
};

osync_hal_return_t osync_hal_get_country_code(
        const char *if_name,
        char *buf,
        size_t bufsz)
{
    INT ret;
    INT radioIndex;
    CHAR tmp_buf[WIFIHAL_MAX_BUFFER];
    const char *str;

    if (!radio_ifname_to_idx(if_name, &radioIndex))
    {
        return OSYNC_HAL_FAILURE;
    }

    memset(tmp_buf, 0, sizeof(tmp_buf));
    ret = wifi_getRadioCountryCode(radioIndex, tmp_buf);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to get country code", if_name);
        return OSYNC_HAL_FAILURE;
    }

    str = c_get_str_by_strkey(g_map_country_str, tmp_buf);
    if (strlen(str) == 0)
    {
        // The country code is not in the map.
        // Pass-through the value from HAL, but convert
        // it to the uppercase letters.
        str = str_toupper(tmp_buf);
    }
    strscpy(buf, str, bufsz);

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_COUNTRY_CODE */
