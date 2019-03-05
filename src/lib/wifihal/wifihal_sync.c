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
 * wifihal_sync.c
 *
 * RDKB Platform - Wifi HAL - Mesh Syncing
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
#include <ev.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <mesh/meshsync_msgs.h>

#include "os.h"
#include "log.h"
#include "const.h"
#include "evsched.h"
#include "wifihal.h"
#include "target.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID               LOG_MODULE_ID_HAL

#define WIFIHAL_SYNC_RETRY      3
#define DHCP_RESYNC_FILE        "/tmp/dnsmasq.leases"

/*****************************************************************************/

static c_item_t map_msg_name[] = {
    C_ITEM_STR(MESH_WIFI_RESET,                     "MESH_WIFI_RESET"),
    C_ITEM_STR(MESH_WIFI_RADIO_CHANNEL,             "MESH_WIFI_RADIO_CHANNEL"),
    C_ITEM_STR(MESH_WIFI_RADIO_CHANNEL_MODE,        "MESH_WIFI_RADIO_CHANNEL_MODE"),
    C_ITEM_STR(MESH_WIFI_SSID_NAME,                 "MESH_WIFI_SSID_NAME"),
    C_ITEM_STR(MESH_WIFI_SSID_ADVERTISE,            "MESH_WIFI_SSID_ADVERTISE"),
    C_ITEM_STR(MESH_WIFI_AP_SECURITY,               "MESH_WIFI_AP_SECURITY"),
    C_ITEM_STR(MESH_WIFI_AP_KICK_ASSOC_DEVICE,      "MESH_WIFI_AP_KICK_ASSOC_DEVICE"),
    C_ITEM_STR(MESH_WIFI_AP_KICK_ALL_ASSOC_DEVICES, "MESH_WIFI_AP_KICK_ALL_ASSOC_DEVICES"),
    C_ITEM_STR(MESH_WIFI_AP_ADD_ACL_DEVICE,         "MESH_WIFI_AP_ADD_ACL_DEVICE"),
    C_ITEM_STR(MESH_WIFI_AP_DEL_ACL_DEVICE,         "MESH_WIFI_AP_DEL_ACL_DEVICE"),
    C_ITEM_STR(MESH_WIFI_MAC_ADDR_CONTROL_MODE,     "MESH_WIFI_MAC_ADDR_CONTROL_MODE"),
    C_ITEM_STR(MESH_SUBNET_CHANGE,                  "MESH_SUBNET_CHANGE"),
    C_ITEM_STR(MESH_URL_CHANGE,                     "MESH_URL_CHANGE"),
    C_ITEM_STR(MESH_WIFI_STATUS,                    "MESH_WIFI_STATUS"),
    C_ITEM_STR(MESH_WIFI_ENABLE,                    "MESH_WIFI_ENABLE"),
    C_ITEM_STR(MESH_STATE_CHANGE,                   "MESH_STATE_CHANGE"),
    C_ITEM_STR(MESH_WIFI_TXRATE,                    "MESH_WIFI_TXRATE"),
    C_ITEM_STR(MESH_CLIENT_CONNECT,                 "MESH_CLIENT_CONNECT"),
    C_ITEM_STR(MESH_DHCP_RESYNC_LEASES,             "MESH_DHCP_RESYNC_LEASES"),
    C_ITEM_STR(MESH_DHCP_ADD_LEASE,                 "MESH_DHCP_ADD_LEASE"),
    C_ITEM_STR(MESH_DHCP_REMOVE_LEASE,              "MESH_DHCP_REMOVE_LEASE"),
    C_ITEM_STR(MESH_DHCP_UPDATE_LEASE,              "MESH_DHCP_UPDATE_LEASE"),
};

static c_item_t map_iface_type[] = {
    C_ITEM_STR(MESH_IFACE_NONE,                     "None"),
    C_ITEM_STR(MESH_IFACE_ETHERNET,                 "Ethernet"),
    C_ITEM_STR(MESH_IFACE_MOCA,                     "MoCA"),
    C_ITEM_STR(MESH_IFACE_WIFI,                     "Wifi"),
    C_ITEM_STR(MESH_IFACE_OTHER,                    "Other"),
};

static c_item_t map_iface_mltype[] = {
    C_ITEM_VAL(MESH_IFACE_ETHERNET,                 WIFIHAL_MACLEARN_TYPE_ETH),
    C_ITEM_VAL(MESH_IFACE_MOCA,                     WIFIHAL_MACLEARN_TYPE_MOCA),
};


static wifihal_sync_mgr_t   wifihal_sync_mgr;
static ev_io                wifihal_sync_evio;
static bool                 wifihal_sync_initialized = false;
static int                  wifihal_sync_fd          = -1;

/*****************************************************************************/

static void                 wifihal_sync_reconnect(void);

/*****************************************************************************/

static char *
wifihal_sync_message_name(int msg_id)
{
    static char         tmp[32];
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_msg_name, msg_id)))
    {
        return c_get_str_by_key(map_msg_name, msg_id);
    }

    snprintf(ARRAY_AND_SIZE(tmp), "ID.%d", msg_id);
    return tmp;
}

static char *
wifihal_sync_iface_name(int iface_type)
{
    static char         tmp[32];
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_iface_type, iface_type)))
    {
        return c_get_str_by_key(map_iface_type, iface_type);
    }

    snprintf(ARRAY_AND_SIZE(tmp), "IFT.%d", iface_type);
    return tmp;
}

static int
wiifhal_sync_iface_mltype(int iface_type)
{
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_iface_mltype, iface_type)))
    {
        return (int)citem->value;
    }

    return -1;
}

static void
wifihal_sync_dhcp_to_dlip(MeshWifiDhcpLease *lease, struct schema_DHCP_leased_IP *dip)
{
    memset(dip, 0, sizeof(*dip));
    strlcpy(dip->hwaddr, lease->mac, sizeof(dip->hwaddr));
    strlcpy(dip->inet_addr, lease->ipaddr, sizeof(dip->inet_addr));
    strlcpy(dip->hostname, lease->hostname, sizeof(dip->hostname));
    strlcpy(dip->fingerprint, lease->fingerprint, sizeof(dip->fingerprint));

    return;
}

static void
wifihal_sync_process_msg(MeshSync *mp)
{
    struct schema_DHCP_leased_IP    dlip;
    wifihal_cloud_mode_t            cloud_mode;
    wifihal_radio_t                 *radio = NULL;
    wifihal_ssid_t                  *ssid = NULL;
    bool                            radios_updated = false;
    bool                            vifs_updated = false;
    bool                            vif_updated = false;
    char                            *ifname;
    char                            buf[64];
    int                             mltype;


#define MK_SSID_IFNAME(idx)     do { \
                                    snprintf(ARRAY_AND_SIZE(buf), "ssid.%d", idx); \
                                    ifname = buf; \
                                } while(0)

#define MK_RADIO_IFNAME(idx)    do { \
                                    snprintf(ARRAY_AND_SIZE(buf), "radio.%d", idx); \
                                    ifname = buf; \
                                } while(0)

#define BREAK_IF_NOT_MGR(x)     if (wifihal_sync_mgr != WIFIHAL_SYNC_MGR_##x) \
                                    break; \
                                else \
                                    LOGD("Sync Received message '%s' from Mesh-Agent", \
                                         wifihal_sync_message_name(mp->msgType))

    switch (mp->msgType) {

    case MESH_WIFI_SSID_NAME:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiSSIDName.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiSSIDName.index);
        }
        LOGI("... %s changed SSID to '%s'", ifname, mp->data.wifiSSIDName.ssid);
        vif_updated = true;
        break;

    case MESH_WIFI_AP_SECURITY:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiAPSecurity.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiAPSecurity.index);
        }
        LOGI("... %s changed Security to: pass '%s', sec '%s', enc '%s'",
             ifname,
             mp->data.wifiAPSecurity.passphrase,
             mp->data.wifiAPSecurity.secMode,
             mp->data.wifiAPSecurity.encryptMode);
        vif_updated = true;
        break;

    case MESH_WIFI_AP_ADD_ACL_DEVICE:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiAPAddAclDevice.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiAPAddAclDevice.index);
        }
        LOGI("... %s added '%s' to ACL", ifname, mp->data.wifiAPAddAclDevice.mac);
        vif_updated = true;
        break;

    case MESH_WIFI_AP_DEL_ACL_DEVICE:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiAPDelAclDevice.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiAPDelAclDevice.index);
        }
        LOGI("... %s deleted '%s' from ACL", ifname, mp->data.wifiAPDelAclDevice.mac);
        vif_updated = true;
        break;

    case MESH_WIFI_MAC_ADDR_CONTROL_MODE:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiMacAddrControlMode.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiMacAddrControlMode.index);
        }
        LOGI("... %s ACL mode change, enable '%s', blacklist '%s'",
             ifname,
             mp->data.wifiMacAddrControlMode.isEnabled ? "true" : "false",
             mp->data.wifiMacAddrControlMode.isBlacklist ? "true" : "false");
        vif_updated = true;
        break;

    case MESH_WIFI_SSID_ADVERTISE:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiSSIDAdvertise.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiSSIDAdvertise.index);
        }
        LOGI("... %s SSID advertise now '%s'",
             ifname,
             mp->data.wifiSSIDAdvertise.enable ? "true" : "false");
        vif_updated = true;
        break;

    case MESH_URL_CHANGE:
        BREAK_IF_NOT_MGR(CM);
        LOGI("... Backhaul URL changed to '%s'", mp->data.url.url);
        wifihal_fatal_restart(false, "Backhaul URL was changed");
        break;

    case MESH_WIFI_RESET:
        BREAK_IF_NOT_MGR(WM);
        LOGI("... Wifi Reset '%s'", mp->data.wifiReset.reset ? "true" : "false");
        break;

    case MESH_SUBNET_CHANGE:
        BREAK_IF_NOT_MGR(NM);
        LOGI("... Subnet change, gwIP '%s', nmask '%s'", mp->data.subnet.gwIP, mp->data.subnet.netmask);
        wifihal_fatal_restart(false, "User subnet was changed");
        break;

    case MESH_WIFI_AP_KICK_ALL_ASSOC_DEVICES:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiAPKickAllAssocDevices.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiAPKickAllAssocDevices.index);
        }
        LOGI("... %s: Kick all devices", ifname);
        break;

    case MESH_WIFI_AP_KICK_ASSOC_DEVICE:
        BREAK_IF_NOT_MGR(WM);
        if ((ssid = wifihal_ssid_by_index(mp->data.wifiAPKickAssocDevice.index)))
        {
            ifname = ssid->ifname;
        }
        else
        {
            MK_SSID_IFNAME(mp->data.wifiAPKickAssocDevice.index);
        }
        LOGI("... %s: Kick device '%s'", ifname, mp->data.wifiAPKickAssocDevice.mac);
        break;

    case MESH_WIFI_RADIO_CHANNEL:
        BREAK_IF_NOT_MGR(WM);
        if ((radio = wifihal_radio_by_index(mp->data.wifiRadioChannel.index)))
        {
            ifname = radio->ifname;
        }
        else
        {
            MK_RADIO_IFNAME(mp->data.wifiRadioChannel.index);
        }
        LOGI("... %s: changed channel to %d", ifname, mp->data.wifiRadioChannel.channel);
        break;

    case MESH_WIFI_RADIO_CHANNEL_MODE:
        BREAK_IF_NOT_MGR(WM);
        if ((radio = wifihal_radio_by_index(mp->data.wifiRadioChannelMode.index)))
        {
            ifname = radio->ifname;
        }
        else
        {
            MK_RADIO_IFNAME(mp->data.wifiRadioChannelMode.index);
        }
        LOGI("... %s: changed mode to '%s', gOnly '%s', nOnly '%s', acOnly '%s'",
             ifname,
             mp->data.wifiRadioChannelMode.channelMode,
             mp->data.wifiRadioChannelMode.gOnlyFlag ? "true" : "false",
             mp->data.wifiRadioChannelMode.nOnlyFlag ? "true" : "false",
             mp->data.wifiRadioChannelMode.acOnlyFlag ? "true" : "false");
        vifs_updated = true;
        break;

    case MESH_STATE_CHANGE:
        BREAK_IF_NOT_MGR(WM);
        LOGI("... Updating cloud mode from mesh state");
        if (mp->data.meshState.state == MESH_STATE_FULL) {
            cloud_mode = WIFIHAL_CLOUD_MODE_FULL;
        } else {
            cloud_mode = WIFIHAL_CLOUD_MODE_MONITOR;
        }
        if (wifihal_cloud_mode_set(cloud_mode)) {
            radios_updated = true;
        } else {
            LOGE("Failed to set new cloud mode %d", cloud_mode);
        }
        break;

    case MESH_CLIENT_CONNECT:
        BREAK_IF_NOT_MGR(NM);
        LOGD("... %s client '%s' (%s) %sconnected",
             wifihal_sync_iface_name(mp->data.meshConnect.iface),
             mp->data.meshConnect.mac,
             mp->data.meshConnect.host,
             mp->data.meshConnect.isConnected ? "" : "dis");

        if ((mltype = wiifhal_sync_iface_mltype(mp->data.meshConnect.iface)) >= 0)
        {
            wifihal_maclearn_update(
                    mltype,
                    mp->data.meshConnect.mac,
                    mp->data.meshConnect.isConnected);
        }
        break;

    case MESH_DHCP_RESYNC_LEASES:
        BREAK_IF_NOT_MGR(WM);
        LOGD("... Resyncing DHCP leases from \"%s\"", DHCP_RESYNC_FILE);
        (void)target_dhcp_lease_parse_file(DHCP_RESYNC_FILE);
        unlink(DHCP_RESYNC_FILE);
        break;


    case MESH_DHCP_UPDATE_LEASE:
    case MESH_DHCP_ADD_LEASE:
        BREAK_IF_NOT_MGR(WM);
        LOGD("... %s DHCP Lease: mac=\"%s\", ipaddr=\"%s\", hn=\"%s\", fp=\"%s\"",
             (mp->msgType == MESH_DHCP_ADD_LEASE) ? "Add" : "Update",
             mp->data.meshLease.mac,
             mp->data.meshLease.ipaddr,
             mp->data.meshLease.hostname,
             mp->data.meshLease.fingerprint);
        wifihal_sync_dhcp_to_dlip(&mp->data.meshLease, &dlip);
        (void)target_dhcp_lease_upsert(&dlip);
        break;

    case MESH_DHCP_REMOVE_LEASE:
        BREAK_IF_NOT_MGR(WM);
        LOGD("... Remove DHCP Lease: mac=\"%s\", ipaddr=\"%s\", hn=\"%s\", fp=\"%s\"",
             mp->data.meshLease.mac,
             mp->data.meshLease.ipaddr,
             mp->data.meshLease.hostname,
             mp->data.meshLease.fingerprint);
        wifihal_sync_dhcp_to_dlip(&mp->data.meshLease, &dlip);
        (void)target_dhcp_lease_remove(&dlip);
        break;

        BREAK_IF_NOT_MGR(WM);
        break;

    default:
        // Only announce "unknown/unsupported" messages in WM
        BREAK_IF_NOT_MGR(WM);
        break;

    }
#undef MK_SSID_IFNAME
#undef MK_RADIO_IFNAME

    if (radios_updated)
    {
        wifihal_radio_all_updated();
    }
    else if (radio && vifs_updated)
    {
        wifihal_vif_conf_all_updated(radio);
    }
    else if (ssid && vif_updated)
    {
        // Schedule task to update VIF
        wifihal_vif_conf_updated(ssid);
    }

    return;
}

static void
wifihal_sync_evio_cb(struct ev_loop *loop, ev_io *watcher, int revents)
{
    MeshSync            mmsg;
    int                 ret;

    if (revents & EV_ERROR)
    {
        LOGE("Sync client MSGQ reported a socket error, reconnecting...");
        wifihal_sync_reconnect();
        return;
    }

    ret = recv(wifihal_sync_fd, &mmsg, sizeof(mmsg), MSG_DONTWAIT);
    if (ret <= 0)
    {
        LOGE("Sync client failed to read message, errno %d, reconnecting...", errno);
        wifihal_sync_reconnect();
        return;
    }
    else if (ret != sizeof(mmsg))
    {
        LOGE("Sync client received malformed packet (length %d != %d)", ret, sizeof(mmsg));
        return;
    }

    if (mmsg.msgType >= MESH_SYNC_MSG_TOTAL)
    {
        LOGE("Sync client received unsupported msg type (%d >= %d)",
             mmsg.msgType, MESH_SYNC_MSG_TOTAL);
        return;
    }

    wifihal_sync_process_msg(&mmsg);
    return;
}

static bool
wifihal_sync_send_msg(MeshSync *mp)
{
    int                 ret;

    if (wifihal_sync_fd < 0) {
        LOGE("Sync could not send message type \"%s\", not connected...",
             wifihal_sync_message_name(mp->msgType));
        return false;
    }

    ret = send(wifihal_sync_fd, (void *)mp, sizeof(*mp), MSG_DONTWAIT);
    if (ret <= 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOGW("Sync client was too busy to send message type \"%s\", dropped...",
                 wifihal_sync_message_name(mp->msgType));
        } else {
            LOGE("Sync client failed to send message type \"%s\", errno = %d",
                 wifihal_sync_message_name(mp->msgType), errno);
        }

        return false;
    }

    return true;
}

void
wifihal_sync_task_connect(void *arg)
{
    struct sockaddr_un  addr;
    const char          *uds_path = MESH_SOCKET_PATH_NAME;
    int                 ret;
    int                 fd;

    (void)arg;

    // Setup address of socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (*uds_path == '\0')
    {
        // Abstract socket name
        *addr.sun_path = '\0';
        strncpy(addr.sun_path+1, uds_path+1, sizeof(addr.sun_path)-2);
    }
    else
    {
        // Socket name on file system
        strncpy(addr.sun_path, uds_path, sizeof(addr.sun_path)-1);
    }

    // Create socket
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOGE("Sync client failed to connect -- socket creation failure, errno = %d", errno);
        evsched_task_reschedule_ms(EVSCHED_SEC(WIFIHAL_SYNC_RETRY));
        return;
    }

    // Connect to Mesh-Agent
    ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        LOGE("Sync client failed to connect, errno = %d", errno);
        close(fd);
        evsched_task_reschedule_ms(EVSCHED_SEC(WIFIHAL_SYNC_RETRY));
        return;
    }

    // Add to libev
    ev_io_init(&wifihal_sync_evio, wifihal_sync_evio_cb, fd, EV_READ);
    ev_io_start(wifihal_evloop, &wifihal_sync_evio);

    LOGI("Sync client connected to Mesh-Agent (fd %d)", fd);
    wifihal_sync_fd = fd;

    switch (wifihal_sync_mgr)
    {
    case WIFIHAL_SYNC_MGR_CM:
        // We need to initialize device config (cloud connection)
        wifihal_device_config_init();
        break;

    case WIFIHAL_SYNC_MGR_WM:
        // We need to send our current cloud mode
        wifihal_cloud_mode_sync();
        break;

    default:
        break;
    }

    return;
}

static void
wifihal_sync_disconnect(void)
{
    // Cancel any connection tasks
    evsched_task_cancel_by_find(&wifihal_sync_task_connect, NULL, EVSCHED_FIND_BY_FUNC);

    // Nothing else to do if not connected
    if (wifihal_sync_fd < 0) {
        return;
    }

    // Stop EVIO and disconnect
    ev_io_stop(wifihal_evloop, &wifihal_sync_evio);
    close(wifihal_sync_fd);
    wifihal_sync_fd = -1;

    LOGI("Sync client disconnected from Mesh-Agent");
    return;
}

static void
wifihal_sync_reconnect(void)
{
    // Disconnect current socket if one exists
    wifihal_sync_disconnect();

    // Schedule task to connect
    evsched_task(&wifihal_sync_task_connect, NULL, EVSCHED_SEC(WIFIHAL_SYNC_RETRY));

    return;
}

/*****************************************************************************/

bool
wifihal_sync_init(wifihal_sync_mgr_t mgr)
{
    if (wifihal_sync_initialized) {
        return true;
    }

    // Kick of connect task
    wifihal_sync_reconnect();

    LOGN("Sync client initialized");
    wifihal_sync_initialized = true;
    wifihal_sync_mgr = mgr;

    return true;
}

bool
wifihal_sync_cleanup(void)
{
    if (!wifihal_sync_initialized) {
        return true;
    }

    wifihal_sync_disconnect();

    LOGN("Sync client cleaned up");
    wifihal_sync_initialized = false;

    return true;
}

bool
wifihal_sync_connected(void)
{
    if (wifihal_sync_fd >= 0) {
        return true;
    }

    return false;
}

bool
wifihal_sync_send_ssid_change(wifihal_ssid_t *ssid, const char *new_ssid)
{
    MeshSync        mmsg;

    if (!wifihal_sync_initialized) {
        LOGW("%s: Cannot send SSID update -- sync not initialized", ssid->ifname);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_SSID_NAME;
    mmsg.data.wifiSSIDName.index = ssid->index;
    strncpy(mmsg.data.wifiSSIDName.ssid,
            new_ssid,
            sizeof(mmsg.data.wifiSSIDName.ssid)-1);

    if (!wifihal_sync_send_msg(&mmsg))
    {
        // It reports error
        return false;
    }

    LOGN("%s: MSGQ sent SSID update to '%s'", ssid->ifname, new_ssid);
    return true;
}

bool
wifihal_sync_send_security_change(wifihal_ssid_t *ssid, MeshWifiAPSecurity *sec)
{
    MeshSync        mmsg;

    if (!wifihal_sync_initialized) {
        LOGW("%s: Cannot send security update -- sync not initialized",
             ssid->ifname);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_AP_SECURITY;
    memcpy(&mmsg.data.wifiAPSecurity, sec, sizeof(mmsg.data.wifiAPSecurity));
    mmsg.data.wifiAPSecurity.index = ssid->index;

    if (!wifihal_sync_send_msg(&mmsg))
    {
        // It reports error
        return false;
    }

    LOGN("%s: MSGQ sent security update (sec '%s', enc '%s')",
         ssid->ifname, sec->secMode, sec->encryptMode);
    return true;
}

bool
wifihal_sync_send_status(wifihal_cloud_mode_t mode)
{
    MeshSync        mmsg;

    if (!wifihal_sync_initialized) {
        LOGW("Cannot send cloud mode status update -- sync not initialized");
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));
    mmsg.msgType = MESH_WIFI_STATUS;

    switch (mode)
    {
    default:
    case WIFIHAL_CLOUD_MODE_UNKNOWN:
        LOGI("Not sending cloud mode status update -- mode is unknown");
        return true;

    case WIFIHAL_CLOUD_MODE_MONITOR:
        mmsg.data.wifiStatus.status = MESH_WIFI_STATUS_MONITOR;
        break;

    case WIFIHAL_CLOUD_MODE_FULL:
        mmsg.data.wifiStatus.status = MESH_WIFI_STATUS_FULL;
        break;
    }

    if (!wifihal_sync_send_msg(&mmsg)) {
        // It reports error
        return false;
    }

    LOGN("MSGQ sent cloud mode status update (mode %d)\n",
         mmsg.data.wifiStatus.status);
    return true;
}
