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

#include <stdlib.h>

#include "osn_dhcp.h"

static int osn_dhcp_client;

bool osn_dhcp_client_start(osn_dhcp_client_t *self)
{
    return true;
}

bool osn_dhcp_client_stop(osn_dhcp_client_t *self)
{
    return true;
}

osn_dhcp_client_t* osn_dhcp_client_new(const char *ifname)
{
    /* The actual content of the structure is internal to
     * this module so we can return whatever pointer, as it is
     * not going to be accessed outside of this module anyway.
     */
    return (osn_dhcp_client_t *)&osn_dhcp_client;
}

bool osn_dhcp_client_del(osn_dhcp_client_t *self)
{
    return true;
}

bool osn_dhcp_client_opt_set(
        osn_dhcp_client_t *self,
        enum osn_dhcp_option opt,
        const char *val)
{
    return true;
}

bool osn_dhcp_client_opt_notify_set(
        osn_dhcp_client_t *self,
        osn_dhcp_client_opt_notify_fn_t *fn,
        void *ctx)
{
    return true;
}

bool osn_dhcp_client_opt_request(
        osn_dhcp_client_t *self,
        enum osn_dhcp_option opt,
        bool request)
{
    return true;
}

bool osn_dhcp_client_opt_get(
        osn_dhcp_client_t *self,
        enum osn_dhcp_option opt,
        bool *request,
        const char **value)
{
    *request = false;
    *value = NULL;
    return true;
}

bool osn_dhcp_client_state_get(
        osn_dhcp_client_t *self,
        bool *enabled)
{
    return true;
}
