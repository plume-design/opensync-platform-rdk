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

#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "devinfo.h"
#include "osync_hal.h"
#include "const.h"


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_DEVINFO_GET_CLOUD_MODE
osync_hal_return_t osync_hal_devinfo_get_cloud_mode(osync_hal_devinfo_cloud_mode_t *mode)
{
    char buf[128];

    memset(buf, 0, sizeof(buf));

    if (devinfo_getv(DEVINFO_MESH_STATE, ARRAY_AND_SIZE(buf)))
    {
        *mode = OSYNC_HAL_DEVINFO_CLOUD_MODE_MONITOR;
        if (!strncmp(buf, "Full", 4) || !strncmp(buf, "full", 4))
        {
            *mode = OSYNC_HAL_DEVINFO_CLOUD_MODE_FULL;
        }
        return OSYNC_HAL_SUCCESS;
    }

    return OSYNC_HAL_FAILURE;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_DEVINFO_GET_CLOUD_MODE */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_DEVINFO_GET_REDIRECTOR_ADDR
osync_hal_return_t osync_hal_devinfo_get_redirector_addr(
        char *buf,
        size_t bufsz)
{
    if (!devinfo_getv(DEVINFO_MESH_URL, buf, bufsz))
    {
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_DEVINFO_GET_REDIRECTOR_ADDR */
