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
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>

#include "log.h"
#include "dmcli.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_OSA

#define DMCLI_ERT_CMD_FMT       "/usr/bin/dmcli eRT getv %s | grep value | ( read a b c d e; echo $e )"
#define DMCLI_ERT_MAX_CMD       DMCLI_MAX_PATH + 70

/*****************************************************************************/

bool
dmcli_eRT_getv(const char *path, char *dest, size_t destsz, bool empty_ok)
{
    char        cmd[DMCLI_ERT_MAX_CMD];
    FILE        *f1;
    int         ret;

    if (strlen(path) > DMCLI_MAX_PATH) {
        LOGE("dmcli_eRT_getv(%s) - Path too long, %d bytes max", path, DMCLI_MAX_PATH);
        return false;
    }

    ret = snprintf(cmd, sizeof(cmd)-1, DMCLI_ERT_CMD_FMT, path);
    if (ret >= (int)(sizeof(cmd)-1)) {
        LOGE("dmcli_eRT_getv(%s) - Command too long!", path);
        return false;
    }

    f1 = popen(cmd, "r");
    if (!f1) {
        LOGE("dmcli_eRT_getv(%s) - popen failed, errno = %d", path, errno);
        return false;
    }

    if (fgets(dest, destsz, f1) == NULL) {
        LOGE("dmcli_eRT_getv(%s) - reading failed, errno = %d", path, errno);
        pclose(f1);
        return false;
    }
    pclose(f1);

    while (dest[strlen(dest)-1] == '\r' || dest[strlen(dest)-1] == '\n') {
        dest[strlen(dest)-1] = '\0';
    }

    if (!empty_ok && strlen(dest) == 0) {
        return false;
    }

    return true;
}
