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

#include "os.h"
#include "log.h"
#include "const.h"
#include "target.h"

#ifdef LOG_ENABLE_RDKLOG
#include "pl2rl.h"
#endif

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_TARGET

/*****************************************************************************/

btrace_type target_get_btrace_type(void)
{
    return BTRACE_LOG_ONLY;
}

/*****************************************************************************/

#ifdef LOG_ENABLE_RDKLOG

static logger_fn_t          logger_rdk_log;
static logger_t             logger_rdk;

static bool logger_rdk_new(logger_t *self)
{
    memset(self, 0, sizeof(*self));
    self->logger_fn = logger_rdk_log;

    pl2rl_init();

    return true;
}

static void logger_rdk_log(logger_t *self, logger_msg_t *msg)
{
    // Attempt to send message
    pl2rl_log(msg);

    return;
}

bool target_log_open(char *name, int flags)
{
    if (log_open(name, flags) == true)
    {
        logger_rdk_new(&logger_rdk);
        log_register_logger(&logger_rdk);

        return true;  /* SUCCESS */
    }

    return false;
}

#endif /* LOG_ENABLE_RDKLOG */

/*****************************************************************************/
