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
 * wifihal_clients_wpa.c
 *
 * RDKB Platform - Wifi HAL - Clients through WPA Control socket
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/ether.h>
#include <ev.h>
#include <wpa_ctrl.h>

#include "os.h"
#include "os_nif.h"
#include "evsched.h"
#include "log.h"
#include "const.h"
#include "wifihal.h"
#include "target.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID           LOG_MODULE_ID_HAL

#define WPACTRL_DIR         "/var/run/hostapd"
#define WPACTRL_RETRY       10
#define WPACTRL_PING_IVAL   5

/*****************************************************************************/

static bool     wifihal_wpactrl_evio_stop(wifihal_ssid_t *ssid);
static bool     wifihal_wpactrl_evio_restart(wifihal_ssid_t *ssid);
static bool     wifihal_wpactrl_start_ssid(wifihal_ssid_t *ssid);
static bool     wifihal_wpactrl_stop_ssid(wifihal_ssid_t *ssid);
static bool     wifihal_wpactrl_cmd_sta(wifihal_ssid_t *ssid, const char *cmd, char *addr, int addr_len);
static bool     wifihal_wpactrl_macaddr_trunc(char *data, char **next);
static void     wifihal_wpactrl_handle_event(wifihal_ssid_t *ssid, char *id, char *data);
static void     wifihal_wpactrl_parse_events(wifihal_ssid_t *ssid, char *buf);
static void     wifihal_wpactrl_evio_cb(struct ev_loop *loop, ev_io *watcher, int revents);
static void     wifihal_wpactrl_task_ssid_ctrl(void *arg);
static void     wifihal_wpactrl_ev_connect(wifihal_ssid_t *ssid, char *id, char *data);
static void     wifihal_wpactrl_ev_disconnect(wifihal_ssid_t *ssid, char *id, char *data);
static void     wifihal_clients_wpa_fetch_existing(wifihal_ssid_t *ssid);

/*****************************************************************************/

static c_item_t wifihal_wpactrl_events[] = {
    C_ITEM_STRKEY_CB(AP_STA_CONNECTED_PWD,      wifihal_wpactrl_ev_connect),
    C_ITEM_STRKEY_CB(AP_STA_DISCONNECTED,       wifihal_wpactrl_ev_disconnect)
};

typedef void (*wifihal_wpactrl_ev_handler)(
                wifihal_ssid_t *ssid,
                const char *id, const char *data);

static wifihal_ssid_t   *wifihal_wpactrl_cur_ssid = NULL;

/*****************************************************************************/

wifihal_ssid_t *
wifihal_ssid_by_iowatcher(ev_io *watcher)
{
    ds_dlist_t              *wifihal_radios = wifihal_get_radios();
    wifihal_radio_t         *radio;
    wifihal_ssid_t          *ssid;

    ds_dlist_foreach(wifihal_radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            if (watcher == &ssid->iowatcher) {
                return ssid;
            }
        }
    }

    return NULL;
}

static void
wifihal_wpactrl_recv_msg(char *msg, size_t len)
{
    if (!wifihal_wpactrl_cur_ssid) {
        return;
    }

    msg[len] = '\0';
    wifihal_wpactrl_parse_events(wifihal_wpactrl_cur_ssid, msg);
}

static void
wifihal_wpactrl_send_ping(wifihal_ssid_t *ssid)
{
    const char  *ping_cmd = "PING";
    const char  *resp_val = "PONG";
    char        reply[128];
    size_t      reply_len;

    reply_len = sizeof(reply);
    if (!wifihal_wpactrl_request(ssid, ping_cmd, reply, &reply_len))
    {
        return;
    }

    if (strncmp(reply, resp_val, strlen(resp_val)))
    {
        LOGE("%s: WPA Control PING did not PONG (reply = \"%s\"), reconnecting",
             ssid->ifname, reply);
        wifihal_wpactrl_evio_restart(ssid);
        return;
    }

    return;
}

static void
wifihal_wpactrl_task_ping(void *arg)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ds_dlist_t          *radios;

    (void)arg;

    if ((radios = wifihal_get_radios())) {
        ds_dlist_foreach(radios, radio) {
            ds_dlist_foreach(&radio->ssids, ssid) {
                wifihal_wpactrl_send_ping(ssid);
            }
        }
    }

    evsched_task_reschedule_ms(EVSCHED_SEC(WPACTRL_PING_IVAL));

    return;
}

static bool
wifihal_wpactrl_evio_stop(wifihal_ssid_t *ssid)
{
    if (ssid->wpactrl)
    {
        ev_io_stop(wifihal_evloop, &ssid->iowatcher);
        wpa_ctrl_detach(ssid->wpactrl);
        wpa_ctrl_close(ssid->wpactrl);
        ssid->wpactrl = NULL;
        LOGN("%s: Closed WPA control socket", ssid->ifname);
    }
    else
    {
        evsched_task_cancel_by_find(
                &wifihal_wpactrl_task_ssid_ctrl,
                ssid,
                (EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG));
    }

    return true;
}

static bool
wifihal_wpactrl_evio_restart(wifihal_ssid_t *ssid)
{
    wifihal_wpactrl_evio_stop(ssid);

    LOGI("%s: Starting WPA Control task", ssid->ifname);
    evsched_task(&wifihal_wpactrl_task_ssid_ctrl, ssid, EVSCHED_ASAP);

    return true;
}

static void
wifihal_wpactrl_handle_event(wifihal_ssid_t *ssid, char *id, char *data)
{
    c_item_t        *citem;
    char            *tmp;

    LOGT("%s: Received WPA Event \"%s\", data \"%s\"",
         ssid->ifname, id, data ? data : "(nil)");

    // CAVEAT: WPA definitions include a space at the end...
    if (!(tmp = malloc(strlen(id)+2))) {
        LOGE("wifihal_wpactrl_handle_event: Failed to allocate memory!!");
        return;
    }
    sprintf(tmp, "%s ", id);
    citem = c_get_item_by_strkey(wifihal_wpactrl_events, tmp);
    free(tmp);

    if (citem)
    {
        ((wifihal_wpactrl_ev_handler)(citem->data))(ssid, id, data);
    }

    return;
}

static void
wifihal_wpactrl_parse_events(wifihal_ssid_t *ssid, char *buf)
{
    char        *p, *id, *data, *next;

    // Format is "<n>ID DATA ...<n2>ID2 DATA2 ..."
    if ((next = strchr(buf, '<'))) {
        next++;
    }
    while (next && *next != '\0')
    {
        p = next;
        if ((next = strchr(p, '<'))) {
            *next++ = '\0';
        }

        if (!(id = strchr(p, '>'))) {
            continue;
        }

        id++;

        if ((data = strchr(id, ' '))) {
            *data++ = '\0';
        }

        wifihal_wpactrl_handle_event(ssid, id, data);
    }

    return;
}

static void
wifihal_wpactrl_evio_cb(struct ev_loop *loop, ev_io *watcher, int revents)
{
    wifihal_ssid_t          *ssid;
    size_t                  recv_len, len;
    char                    buf[1024];

    if (!(ssid = wifihal_ssid_by_iowatcher(watcher)))
    {
        LOGE("wifihal_wpactrl_evio_cb() -- SSID not found, stopping watcher");
        ev_io_stop(loop, watcher);
        return;
    }

    if (revents & EV_ERROR)
    {
        LOGE("%s: WPA control socket closed, restarting", ssid->ifname);
        wifihal_wpactrl_evio_restart(ssid);
        return;
    }

    if (wpa_ctrl_pending(ssid->wpactrl) < 0)
    {
        LOGE("%s: WPA control socket has no data, restarting", ssid->ifname);
        wifihal_wpactrl_evio_restart(ssid);
        return;
    }

    recv_len = sizeof(buf) - 1;
    if (wpa_ctrl_recv(ssid->wpactrl, buf, &recv_len) < 0)
    {
        LOGE("%s: WPA control socket failed to receive, restarting", ssid->ifname);
        wifihal_wpactrl_evio_restart(ssid);
        return;
    }

    len = (recv_len < sizeof(buf)) ? recv_len : sizeof(buf) - 1;
    buf[len] = '\0';
    wifihal_wpactrl_parse_events(ssid, buf);

    return;
}

static void
wifihal_wpactrl_task_ssid_ctrl(void *arg)
{
    struct wpa_ctrl         *ctrl;
    wifihal_ssid_t          *ssid = arg;
    char                    ctrl_path[128];
    int                     flags;
    int                     fd;

    if (ssid->wpactrl) {
        // Already initialized?
        return;
    }

    snprintf(ARRAY_AND_SIZE(ctrl_path), "%s/%s", WPACTRL_DIR, ssid->ifname);

    if (!(ctrl = wpa_ctrl_open(ctrl_path)))
    {
        LOGW("%s: Failed to open WPA control socket \"%s\", rescheduling",
             ssid->ifname, ctrl_path);
        evsched_task_reschedule_ms(EVSCHED_SEC(WPACTRL_RETRY));
        return;
    }

    if (wpa_ctrl_attach(ctrl) != 0)
    {
        wpa_ctrl_close(ctrl);
        LOGW("%s: Failed to attach WPA control socket \"%s\", rescheduling",
             ssid->ifname, ctrl_path);
        evsched_task_reschedule_ms(EVSCHED_SEC(WPACTRL_RETRY));
        return;
    }

    if ((fd = wpa_ctrl_get_fd(ctrl)) < 0)
    {
        wpa_ctrl_detach(ctrl);
        wpa_ctrl_close(ctrl);
        LOGW("%s: Failed to get fd for WPA control socket \"%s\", rescheduling",
             ssid->ifname, ctrl_path);
        evsched_task_reschedule_ms(EVSCHED_SEC(WPACTRL_RETRY));
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
    if (flags < 0) {
        LOGW("%s: Failed to set WPA control socket to non-blocking", ssid->ifname);
    }

    ssid->wpafd = fd;
    ssid->wpactrl = ctrl;
    ev_io_init(&ssid->iowatcher, wifihal_wpactrl_evio_cb, fd, EV_READ);
    ev_io_start(wifihal_evloop, &ssid->iowatcher);
    LOGN("%s: Opened WPA control socket \"%s\"", ssid->ifname, ctrl_path);

    // Fetch existing client connections (in case we missed any)
    wifihal_clients_wpa_fetch_existing(ssid);

    return;
}

static bool
wifihal_wpactrl_start_ssid(wifihal_ssid_t *ssid)
{
    return wifihal_wpactrl_evio_restart(ssid);
}

static bool
wifihal_wpactrl_stop_ssid(wifihal_ssid_t *ssid)
{
    return wifihal_wpactrl_evio_stop(ssid);
}

static bool
wifihal_wpactrl_cmd_sta(wifihal_ssid_t *ssid, const char *cmd, char *addr, int addr_len)
{
    char            buf[1024], *p;
    size_t          len;

    len = sizeof(buf);
    if (!wifihal_wpactrl_request(ssid, cmd, buf, &len))
    {
        return false;
    }

    if (!strncmp(buf, "FAIL", 4))
    {
        return false;
    }

    // First line is the client MAC address
    p = buf;
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    *p = '\0';

    if (buf[0] == '\0') {
        // No client addr available
        return false;
    }

    LOGT("%s: wpa_sta (%s) got \"%s\"", ssid->ifname, cmd, buf);

    strncpy(addr, buf, (addr_len-1));

    return true;
}

static bool
wifihal_wpactrl_macaddr_trunc(char *data, char **next)
{
    char    *p;

    if (next) {
        *next = NULL;
    }

    if ((p = strchr(data, ' ')))
    {
        *p = '\0';
        if (next) {
            *next = (char *)p+1;
        }
    }

    if (ether_aton(data)) {
        return true;
    }

    return false;
}

static void
wifihal_wpactrl_ev_connect(wifihal_ssid_t *ssid, char *id, char *data)
{
    wifihal_key_t       *kp;
    char                *psk;
    char                *key_id = WIFIHAL_KEY_DEFAULT;

    if (!wifihal_wpactrl_macaddr_trunc(data, &psk))
    {
        return;
    }

    if (psk)
    {
        kp = wifihal_security_key_find_by_psk(ssid, psk);
        if (kp) {
            key_id = kp->id;
        }
    }

    wifihal_clients_connection(ssid, data, key_id);

    return;
}

static void
wifihal_wpactrl_ev_disconnect(wifihal_ssid_t *ssid, char *id, char *data)
{
    if (!ssid || !data || !wifihal_wpactrl_macaddr_trunc(data, NULL))
    {
        return;
    }

    wifihal_clients_disconnection(ssid, data);
    return;
}

static void
wifihal_clients_wpa_fetch_existing(wifihal_ssid_t *ssid)
{
    char            cmd[64];
    char            mac[32];

    // Fetch first client MAC
    if (!wifihal_wpactrl_cmd_sta(ssid, "STA-FIRST", mac, sizeof(mac)))
    {
        return;
    }

    do {
        // Report it's connection
        wifihal_clients_connection(ssid, mac, NULL);

        // Fetch next client MAC
        snprintf(cmd, sizeof(cmd)-1, "STA-NEXT %s", mac);
    } while (wifihal_wpactrl_cmd_sta(ssid, cmd, mac, sizeof(mac)));

    return;
}


/*****************************************************************************/

bool
wifihal_clients_wpa_init(void)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ds_dlist_t          *radios;

    if (!(radios = wifihal_get_radios()))
    {
        return false;
    }

    ds_dlist_foreach(radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            wifihal_wpactrl_start_ssid(ssid);
        }
    }

    evsched_task(&wifihal_wpactrl_task_ping, NULL, EVSCHED_SEC(WPACTRL_PING_IVAL));

    return true;
}

bool
wifihal_clients_wpa_cleanup(void)
{
    wifihal_radio_t     *radio;
    wifihal_ssid_t      *ssid;
    ds_dlist_t          *radios;

    evsched_task_cancel_by_find(
            &wifihal_wpactrl_task_ping,
            NULL,
            EVSCHED_FIND_BY_FUNC);

    if (!(radios = wifihal_get_radios()))
    {
        return false;
    }

    ds_dlist_foreach(radios, radio) {
        ds_dlist_foreach(&radio->ssids, ssid) {
            wifihal_wpactrl_stop_ssid(ssid);
        }
    }

    return true;
}

bool
wifihal_wpactrl_request(
        wifihal_ssid_t *ssid,
        const char *cmd,
        char *reply,
        size_t *reply_len)
{
    int         ret;

    if (!ssid->wpactrl) {
        return false;
    }

    if (*reply_len < 2) {
        return false;
    }
    (*reply_len)--;  // In case they don't account for null termination

    wifihal_wpactrl_cur_ssid = ssid;
    ret = wpa_ctrl_request(ssid->wpactrl,
                           cmd,
                           strlen(cmd),
                           reply,
                           reply_len,
                           &wifihal_wpactrl_recv_msg);
    wifihal_wpactrl_cur_ssid = NULL;

    if (ret < 0)
    {
        LOGE("%s: WPA Control sending cmd '%s' failed (ret = %d), reconnecting",
             ssid->ifname, cmd, ret);
        wifihal_wpactrl_evio_restart(ssid);
        return false;
    }
    reply[*reply_len] = '\0';

    LOGT("%s: WPA Control cmd '%s' responded with '%s'", ssid->ifname, cmd, reply);
    return true;
}
