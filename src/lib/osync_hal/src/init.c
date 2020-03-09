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

#include "os_nif.h"
#include "osync_hal.h"
#include "target.h"


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INIT
osync_hal_return_t osync_hal_init()
{
    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INIT */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_READY
osync_hal_return_t osync_hal_ready()
{
    if (!os_nif_is_interface_ready(BACKHAUL_IFNAME_2G))
    {
        LOGW("Target not ready, '%s' is not UP", BACKHAUL_IFNAME_2G);
        return OSYNC_HAL_FAILURE;
    }

    if (!os_nif_is_interface_ready(BACKHAUL_IFNAME_5G))
    {
        LOGW("Target not ready, '%s' is not UP", BACKHAUL_IFNAME_5G);
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_READY */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_DEINIT
osync_hal_return_t osync_hal_deinit()
{
    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_DEINIT */
