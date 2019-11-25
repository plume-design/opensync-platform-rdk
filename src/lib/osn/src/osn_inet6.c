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
#include <errno.h>

#include "osn_inet6.h"
#include "osn_types.h"

#include "log.h"
#include "util.h"
#include "const.h"

typedef struct osn_nl osn_nl_t;

struct osn_ip6
{
    int foo;
};

static osn_ip6_t dummy_ip6_t;

osn_ip6_t* osn_ip6_new(const char *ifname)
{
    return &dummy_ip6_t;
}

bool osn_ip6_del(osn_ip6_t *self)
{
    return true;
}

bool osn_ip6_apply(osn_ip6_t *self)
{
    return true;
}

bool osn_ip6_addr_add(
        osn_ip6_t *self,
        const osn_ip6_addr_t *addr)
{
    return true;
}

bool osn_ip6_addr_del(
        osn_ip6_t *ip6,
        const osn_ip6_addr_t *dns)
{
    return true;
}

bool osn_ip6_dns_add(
        osn_ip6_t *ip6,
        const osn_ip6_addr_t *dns)
{
    return false;
}

bool osn_ip6_dns_del(
        osn_ip6_t *ip6,
        const osn_ip6_addr_t *addr)
{
    return false;
}

bool osn_ip6_ipaddr_parse(
        void *_self,
        int type,
        const char *line)
{
    return true;
}

void osn_ip6_status_ipaddr_update(osn_ip6_t *self)
{
    return;
}

bool osn_ip6_neigh_parse(
        void *_self,
        int type,
        const char *line)
{
    return true;
}

void osn_ip6_status_neigh_update(osn_ip6_t *self)
{
    return;
}

void osn_ip6_nl_fn(
        osn_nl_t *nl,
        uint64_t event,
        const char *ifname)
{
    return;
}

void osn_ip6_status_notify(
        osn_ip6_t *self,
        osn_ip6_status_fn_t *fn, void *data)
{
    return;
}
