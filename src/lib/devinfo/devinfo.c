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
#include "devinfo.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_OSA

#define DEVINFO_CMD_FMT         "/usr/sbin/deviceinfo.sh -%s"
#define DEVINFO_CMD_MAX         DEVINFO_MAX_LEN + 32

/*****************************************************************************/

bool
devinfo_getv(const char *what, char *dest, size_t destsz, bool empty_ok)
{
    char        cmd[DEVINFO_CMD_MAX];
    FILE        *f1;
    int         ret;

    if (strlen(what) > DEVINFO_MAX_LEN) {
        LOGE("devinfo_getv(%s) - Item too long, %d bytes max", what, DEVINFO_MAX_LEN);
        return false;
    }

    ret = snprintf(cmd, sizeof(cmd)-1, DEVINFO_CMD_FMT, what);
    if (ret >= (int)(sizeof(cmd)-1)) {
        LOGE("devinfo_getv(%s) - Command too long!", what);
        return false;
    }

    f1 = popen(cmd, "r");
    if (!f1) {
        LOGE("devinfo_getv(%s) - popen failed, errno = %d", what, errno);
        return false;
    }

    if (fgets(dest, destsz, f1) == NULL) {
        LOGE("devinfo_getv(%s) - reading failed, errno = %d", what, errno);
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
