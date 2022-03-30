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
 * pl2rld.c
 *
 * OpenSync Logger to RDK Logger Daemon
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/types.h>
#include <rdk_debug.h>
#include <ev.h>

#include "os_backtrace.h"
#include "log.h"
#include "pl2rl.h"
#include "ds_dlist.h"

#define MODULE_ID           LOG_MODULE_ID_MAIN

#define PL2RLD_CLIENTS_MAX   16
#define PL2RLD_CLIENTS_BUF   4096

#define RDK_LOGGER_INI      "/etc/debug.ini"
#define RDK_LOGGER_MODULE   "LOG.RDK.MeshService"

/*****************************************************************************/

typedef struct
{
    int                 fd;
    ev_io               evio;

    uint32_t            pid;
    char                name[PL2RL_NAME_LEN];
    bool                registered;

    ds_dlist_node_t     dsl_node;
} pclient_t;

/*****************************************************************************/

struct ev_loop     *_ev_loop            = NULL;
ev_signal           _ev_sigterm;
ev_signal           _ev_sigint;
ev_io               _ev_listener;

int                 pl2rld_listener_fd   = -1;
ds_dlist_t          pl2rld_clients;

/*****************************************************************************/

pclient_t *     pl2rld_client_by_evio(ev_io *evio);
bool            pl2rld_client_listener_init(const char *spath);
bool            pl2rld_client_listener_cleanup(void);
bool            pl2rld_client_add(int fd);
void            pl2rld_client_remove(pclient_t *pc);
void            pl2rld_client_cleanup(void);
void            pl2rld_client_recv(pclient_t *pc);
void            pl2rld_client_recv_reg(pclient_t *pc, pl2rl_msg_t *msg);
void            pl2rld_client_recv_log(pclient_t *pc, pl2rl_msg_t *msg, char *text);
void            pl2rld_client_accept_cb(struct ev_loop *loop, ev_io *evio, int revents);
void            pl2rld_client_evio_cb(struct ev_loop *loop, ev_io *evio, int revents);

/*****************************************************************************/

void handle_signal(struct ev_loop *loop, ev_signal *w, int revents)
{
    LOGEM("Received signal %d, triggering shutdown", w->signum);
    ev_break(_ev_loop, EVBREAK_ALL);
    return;
}

bool pl2rld_client_add(int fd)
{
    pclient_t       *pc;

    // Allocate new client
    if (!(pc = calloc(sizeof(*pc), 1))) {
        LOGEM("Failed to add client -- malloc failed");
        return false;
    }

    // Setup EV IO Watcher
    pc->fd = fd;
    ev_io_init(&pc->evio, pl2rld_client_evio_cb, pc->fd, EV_READ);
    ev_io_start(_ev_loop, &pc->evio);

    // Add to client list
    ds_dlist_insert_tail(&pl2rld_clients, pc);

    LOGI("[fd %d] New client connection", pc->fd);
    return true;
}

void pl2rld_client_remove(pclient_t *pc)
{
    LOGI("[fd %d] Removing client connection", pc->fd);

    // Cleanup EV IO Watcher and close socket
    ev_io_stop(_ev_loop, &pc->evio);
    close(pc->fd);

    // Remove from client list
    ds_dlist_remove(&pl2rld_clients, pc);

    // Free client
    free(pc);

    return;
}

void pl2rld_client_cleanup(void)
{
    pclient_t       *pc;

    // Remove all clients
    while ((pc = ds_dlist_head(&pl2rld_clients)))
    {
        pl2rld_client_remove(pc);
    }

    return;
}

pclient_t* pl2rld_client_by_evio(ev_io *evio)
{
    pclient_t       *pc;

    ds_dlist_foreach(&pl2rld_clients, pc) {
        if (evio == &pc->evio) {
            return pc;
        }
    }

    return NULL;
}

void pl2rld_client_evio_cb(struct ev_loop *loop, ev_io *evio, int revents)
{
    pclient_t       *pc;

    if (!(pc = pl2rld_client_by_evio(evio)))
    {
        LOGE("pl2rld_client_evio_cb() -- Client not found, stopping evio");
        ev_io_stop(loop, evio);
        return;
    }

    if (revents & EV_ERROR)
    {
        LOGW("[fd %d] Connection closed", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }

    pl2rld_client_recv(pc);

    return;
}

void pl2rld_client_recv(pclient_t *pc)
{
    pl2rl_msg_hdr_t     *hdr;
    pl2rl_msg_t         *msg;
    char                buf[PL2RLD_CLIENTS_BUF];
    char                *text;
    int                 hdr_len = sizeof(pl2rl_msg_hdr_t);
    int                 rlen;
    int                 ret;

    // Read in header
    ret = read(pc->fd, buf, hdr_len);
    if (ret < 0)
    {
        if (errno == EAGAIN) {
            return;
        }
        LOGE("[fd %d] Error reading from client", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }
    else if (ret != hdr_len) {
        LOGE("[fd %d] Malformed packet received", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }

    // Validate message type
    hdr = (pl2rl_msg_hdr_t *)buf;
    if (hdr->msg_type >= PL2RL_MSG_TYPE_MAX)
    {
        LOGE("[fd %d] Invalid message type received", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }

    // Read in rest of message
    rlen = hdr->length - hdr_len;
    ret = read(pc->fd, buf+hdr_len, rlen);
    if (ret < 0)
    {
        LOGE("[fd %d] Error reading data from client", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }
    else if (ret != rlen) {
        LOGE("[fd %d] Malformed data received", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }

    // Handle message
    msg = (pl2rl_msg_t *)buf;
    switch (msg->hdr.msg_type)
    {
    case PL2RL_MSG_TYPE_REGISTER:
        pl2rld_client_recv_reg(pc, msg);
        break;

    case PL2RL_MSG_TYPE_LOG:
        // Set text pointer
        hdr_len += sizeof(pl2rl_msg_log_data_t);
        text = (char *)buf + hdr_len;
        pl2rld_client_recv_log(pc, msg, text);
        break;
    }

    return;
}

void pl2rld_client_recv_reg(pclient_t *pc, pl2rl_msg_t *msg)
{
    LOGI("[fd %d] Registered as \"%s\", PID %u",
         pc->fd, msg->data.reg.name, msg->data.reg.pid);

    strncpy(pc->name, msg->data.reg.name, sizeof(pc->name)-1);
    pc->pid = msg->data.reg.pid;
    pc->registered = true;

    return;
}

void pl2rld_client_recv_log(pclient_t *pc, pl2rl_msg_t *msg, char *text)
{
    uint32_t            rdk_level = RDK_LOG_DEBUG;
    char                *sev;
    char                *mod;

    if (!pc->registered)
    {
        LOGE("[fd %d] Received LOG message before registration", pc->fd);
        pl2rld_client_remove(pc);
        return;
    }

    LOGT("[fd %d] Received LOG, sev %u, module %u, len %u, \"%.*s\"",
         pc->fd,
         msg->data.log.severity,
         msg->data.log.module,
         msg->data.log.text_len,
         msg->data.log.text_len,
         text);

    // Get names
    sev = log_severity_str(msg->data.log.severity);
    mod = log_module_str(msg->data.log.module);

    // Convert severity to RDK Logger severity
    switch (msg->data.log.severity)
    {
    case LOG_SEVERITY_EMERG:
        rdk_level = RDK_LOG_FATAL;
        break;

    case LOG_SEVERITY_ALERT:
    case LOG_SEVERITY_CRIT:
    case LOG_SEVERITY_ERR:
        rdk_level = RDK_LOG_ERROR;
        break;

    case LOG_SEVERITY_WARNING:
        rdk_level = RDK_LOG_WARN;
        break;

    case LOG_SEVERITY_NOTICE:
        rdk_level = RDK_LOG_NOTICE;
        break;

    case LOG_SEVERITY_INFO:
        rdk_level = RDK_LOG_INFO;
        break;

    case LOG_SEVERITY_TRACE:
        rdk_level = RDK_LOG_TRACE1;
        break;

    default:
    case LOG_SEVERITY_DEBUG:
        rdk_level = RDK_LOG_DEBUG;
        break;
    }

    (void)sev; // currently not used
    RDK_LOG(rdk_level, RDK_LOGGER_MODULE,
            "%s[%u]: %s: %.*s\n",
            pc->name,
            pc->pid,
            mod,
            msg->data.log.text_len,
            text);

    return;
}

bool pl2rld_client_listener_init(const char *spath)
{
    struct sockaddr_un      addr;
    int                     fd;

    // Create our listening socket
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        LOGE("Failed to create OpenSync listener socket, errno = %d (%s)",
             errno, strerror(errno));
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

    // Bind socket
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        LOGE("Failed to bind OpenSync listener socket, errno = %d (%s)",
             errno, strerror(errno));
        close(fd);
        return false;
    }

    // Listen to socket
    if (listen(fd, PL2RLD_CLIENTS_MAX) < 0)
    {
        LOGE("Failed to listen on OpenSync listener socket, errno = %d (%s)",
             errno, strerror(errno));
        close(fd);
        return false;
    }

    // Setup EV IO Watcher
    pl2rld_listener_fd = fd;
    ev_io_init(&_ev_listener, pl2rld_client_accept_cb, fd, EV_READ);
    ev_io_start(_ev_loop, &_ev_listener);

    LOGI("Listening on unix domain socket \"%c%s\"",
         *spath == '\0' ? '@' : *spath, spath+1);
    return true;
}

bool pl2rld_client_listener_cleanup(void)
{
    if (pl2rld_listener_fd >= 0)
    {
        ev_io_stop(_ev_loop, &_ev_listener);
        close(pl2rld_listener_fd);
        pl2rld_listener_fd = -1;
    }

    return true;
}

void pl2rld_client_accept_cb(struct ev_loop *loop, ev_io *evio, int revents)
{
    struct sockaddr_un      addr;
    int                     addr_len = sizeof(addr);
    int                     flags;
    int                     fd;

    // Accept a new client connection
    fd = accept(pl2rld_listener_fd, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
    if (fd < 0) {
        LOGE("Failed to accept new client connection, errno = %d (%s)",
             errno, strerror(errno));
        return;
    }

    // Set socket to non-blocking
    if ((flags = fcntl(fd, F_GETFL, 0)) >= 0)
    {
        flags |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
            flags = -1;
        }
    }
    if (flags < 0)
    {
        LOGE("[fd %d] Failed to set non-blocking, closing", fd);
        close(fd);
        return;
    }

    // Add new client
    pl2rld_client_add(fd);

    return;
}

int main(int argc, char **argv)
{
    bool        background = false;
    int         verbose = 0;
    int         opt;

    // Process command line args
    while ((opt = getopt(argc, argv, "vb")) >= 0)
    {
        switch (opt)
        {
        case 'v':
            verbose++;
            break;

        case 'b':
            background = true;
            break;

        default:
            fprintf(stderr, "Invalid command line option '%c'\n", opt);
            return(1);
        }
    }

    // Check if we need to daemonize
    if (background)
    {
        if (daemon(0, 0) < 0)
        {
            fprintf(stderr, "Failed to daemonize, errno = %d (%s)\n",
                             errno, strerror(errno));
            return(1);
        }
    }

    // Initialize Logging
    log_open("PL2RLD", 0);
    if (verbose >= 2) {
        log_severity_set(LOG_SEVERITY_TRACE);
    } else if (verbose == 1) {
        log_severity_set(LOG_SEVERITY_DEBUG);
    } else {
        log_severity_set(LOG_SEVERITY_INFO);
    }
    LOGN("Starting PL2RLD (OpenSync Logger to RDK Logger Daemon)...");

    // Enable backtrace support
    backtrace_init();

    // Setup libev
    _ev_loop = EV_DEFAULT;

    // Setup signal handler
    ev_signal_init(&_ev_sigterm, handle_signal, SIGTERM);
    ev_signal_start(_ev_loop, &_ev_sigterm);
    ev_signal_init(&_ev_sigint,  handle_signal, SIGINT);
    ev_signal_start(_ev_loop, &_ev_sigint);

    // Initialize client list
    ds_dlist_init(&pl2rld_clients, pclient_t, dsl_node);

    // Initialize RDK Logger
    rdk_logger_init(RDK_LOGGER_INI);

    // Initialize OpenSync Listener
    if (!pl2rld_client_listener_init(PL2RL_SOCKET_PATH))
    {
        fprintf(stderr, "Failed to setup unix domain socket -- exiting\n");
        return(1);
    }

    // Run main loop
    ev_run(_ev_loop, 0);

    // Cleanup & Exit
    LOGW("PL2RLD shutting down...");

    // Cleanup listener
    pl2rld_client_listener_cleanup();

    // Cleanup clients
    pl2rld_client_cleanup();

    ev_default_destroy();

    return(0);
}
