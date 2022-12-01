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

#ifndef DMCLI_H_INCLUDED
#define DMCLI_H_INCLUDED

#define DMCLI_MAX_PATH              128

#define DMCLI_ERT_MODEL_NUM         CONFIG_RDK_DM_MODEL_NUM
#define DMCLI_ERT_SERIAL_NUM        CONFIG_RDK_DM_SERIAL_NUM
#define DMCLI_ERT_SOFTWARE_VER      CONFIG_RDK_DM_SOFTWARE_VER
#define DMCLI_ERT_CM_MAC            CONFIG_RDK_DM_CM_MAC
#define DMCLI_ERT_MESH_STATE        CONFIG_RDK_DM_MESH_STATE
#define DMCLI_ERT_MESH_URL          CONFIG_RDK_DM_MESH_URL

extern bool     dmcli_eRT_getv(const char *path, char *dest, size_t destsz, bool empty_ok);

#endif /* DMCLI_H_INCLUDED */
