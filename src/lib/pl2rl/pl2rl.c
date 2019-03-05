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

/*
 * pl2rl.c
 *
 * OpenSync Logger to RDK Logger Library
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/types.h>

#include "log.h"
#include "pl2rl.h"

/*****************************************************************************/

#define PL2RL_CONNECT_RATE          1       // Only attempt once a second
#define PL2RL_RETRIES               3       // EAGAIN retries
#define PL2RL_BUF                   4096    // Max buffer size

/*****************************************************************************/

static time_t   pl2rl_last_attempt  =  0;
static int      pl2rl_fd            = -1;

/*****************************************************************************/

static void
pl2rl_disconnect(void)
{
    if (pl2rl_fd < 0) {
        return;
    }

    close(pl2rl_fd);

    pl2rl_last_attempt =  0;
    pl2rl_fd           = -1;

    return;
}

static bool
pl2rl_send(void *data, int len)
{
    int     tries;
    int     ret;
    int     w = 0;

    if (pl2rl_fd < 0) {
        return false;
    }

    for (tries = PL2RL_RETRIES; tries > 0; tries--)
    {
        ret = send(pl2rl_fd, (data+w), (len-w), MSG_NOSIGNAL);
        if (ret < 0)
        {
            if (errno == EAGAIN)
            {
                continue;
            }
            pl2rl_disconnect();
            return false;
        }
        w += ret;
        if (w == len)
        {
            break;
        }
    }

    if (tries == 0)
    {
        return false;
    }

    return true;
}

static bool
pl2rl_connect(void)
{
    struct sockaddr_un  addr;
    pl2rl_msg_t        *msg;
    time_t              now = time(NULL);
    char                buf[PL2RL_BUF];
    char               *spath = PL2RL_SOCKET_PATH;
    int                 flags;
    int                 fd;

    if (pl2rl_fd >= 0) {
        return true;
    }

    if (pl2rl_last_attempt)
    {
        if ((now - pl2rl_last_attempt) < PL2RL_CONNECT_RATE) {
            // Rate limit the connection
            return false;
        }
    }
    pl2rl_last_attempt = now;

    // Create socket
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        return false;
    }

    // Setup address of socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*spath == '\0')
    {
        // Abstract socket name
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path+1, spath+1, sizeof(addr.sun_path)-2);
    }
    else
    {
        // Socket name on file system
        unlink(spath);
        strncpy(addr.sun_path, spath, sizeof(addr.sun_path)-1);
    }

    // Connect to PL2RL Daemon
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        // Failed to connect
        close(fd);
        return false;
    }

    // Set socket to non-blocking
    if ((flags = fcntl(fd, F_GETFL, 0)) >= 0)
    {
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        {
            flags = -1;
        }
    }
    if (flags < 0) {
        // Couldn't set to non-blocking
        close(fd);
        return false;
    }

    // Connected
    pl2rl_last_attempt = 0;
    pl2rl_fd           = fd;

    // Register
    msg = (pl2rl_msg_t *)buf;
    memset(msg, 0, sizeof(*msg));
    msg->hdr.msg_type = PL2RL_MSG_TYPE_REGISTER;
    msg->hdr.length   = sizeof(pl2rl_msg_hdr_t) + sizeof(pl2rl_msg_reg_data_t);

    msg->data.reg.pid = getpid();
    snprintf(msg->data.reg.name,
             sizeof(msg->data.reg.name)-1,
             "%s",
             log_get_name());

    if (!pl2rl_send(buf, msg->hdr.length))
    {
        // Couldn't register, disconnect
        pl2rl_disconnect();
        return false;
    }

    return true;
}

/*****************************************************************************/

bool
pl2rl_init(void)
{
    // Attempt to connect now
    pl2rl_connect();

    return true;
}

void
pl2rl_log(logger_msg_t *lmsg)
{
    pl2rl_msg_t    *msg;
    char            buf[PL2RL_BUF];
    char            *text;
    int             text_len = strlen(lmsg->lm_text);
    int             hdr_len = sizeof(pl2rl_msg_hdr_t) + sizeof(pl2rl_msg_log_data_t);

    if (pl2rl_fd < 0)
    {
        if (!pl2rl_connect())
        {
            // Can't connect: Drop message
            return;
        }
    }

    msg = (pl2rl_msg_t *)buf;
    msg->hdr.msg_type      = PL2RL_MSG_TYPE_LOG;
    msg->hdr.length        = hdr_len + text_len;
    msg->data.log.severity = lmsg->lm_severity;
    msg->data.log.module   = lmsg->lm_module;
    msg->data.log.text_len = text_len;

    if (msg->hdr.length > sizeof(buf)) {
        // Buffer too small
        return;
    }
    text = (char *)buf + hdr_len;
    memcpy(text, lmsg->lm_text, text_len);

    pl2rl_send(buf, msg->hdr.length);

    return;
}
