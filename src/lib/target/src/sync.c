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
 * sync.c
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

#include <mesh/meshsync_msgs.h>  // this file is included by vendor

#include "os.h"
#include "log.h"
#include "const.h"
#include "target.h"
#include "osync_hal.h"
#include "target_internal.h"
#include "devinfo.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

/*****************************************************************************/

#define MODULE_ID               LOG_MODULE_ID_HAL

#define SYNC_RETRY      3

/*****************************************************************************/

static c_item_t map_msg_name[] =
{
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

static c_item_t map_iface_type[] =
{
    C_ITEM_STR(MESH_IFACE_NONE,                     "None"),
    C_ITEM_STR(MESH_IFACE_ETHERNET,                 "Ethernet"),
    C_ITEM_STR(MESH_IFACE_MOCA,                     "MoCA"),
    C_ITEM_STR(MESH_IFACE_WIFI,                     "Wifi"),
    C_ITEM_STR(MESH_IFACE_OTHER,                    "Other"),
};

static c_item_t map_iface_mltype[] =
{
    C_ITEM_VAL(MESH_IFACE_ETHERNET,                 MACLEARN_TYPE_ETH),
    C_ITEM_VAL(MESH_IFACE_MOCA,                     MACLEARN_TYPE_MOCA),
};

static sync_on_connect_cb_t sync_on_connect_cb = NULL;
static sync_mgr_t           sync_mgr;
static ev_io                sync_evio;
static bool                 sync_initialized = false;
static int                  sync_fd          = -1;

static ev_timer sync_timer;

/*****************************************************************************/

static void                 sync_reconnect(void);

/*****************************************************************************/

static char* sync_message_name(int msg_id)
{
    static char         tmp[32];
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_msg_name, msg_id)) != NULL)
    {
        return c_get_str_by_key(map_msg_name, msg_id);
    }

    snprintf(ARRAY_AND_SIZE(tmp), "ID.%d", msg_id);
    return tmp;
}

static char* sync_iface_name(int iface_type)
{
    static char         tmp[32];
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_iface_type, iface_type)) != NULL)
    {
        return c_get_str_by_key(map_iface_type, iface_type);
    }

    snprintf(ARRAY_AND_SIZE(tmp), "IFT.%d", iface_type);
    return tmp;
}

static int wiifhal_sync_iface_mltype(int iface_type)
{
    c_item_t            *citem;

    if ((citem = c_get_item_by_key(map_iface_mltype, iface_type)) != NULL)
    {
        return (int)citem->value;
    }

    return -1;
}

static void resync_leases()
{
    if (osync_hal_dhcp_resync_all(dhcp_lease_upsert) != OSYNC_HAL_SUCCESS)
    {
        LOGE("Failed to resync DHCP leases");
    }
    dhcp_server_status_dispatch();
}

static void sync_process_msg(MeshSync *mp)
{
    radio_cloud_mode_t              cloud_mode;
    radio_cloud_mode_t              current_cloud_mode;
    INT                             radioIndex;
    INT                             ret;
    char                            radio_ifname[128];
    char                            ssid_ifname[128];
    int                             mltype;


#define MK_SSID_IFNAME(idx)     do { \
                                    snprintf(ARRAY_AND_SIZE(buf), "ssid.%d", idx); \
                                    ifname = buf; \
                                } while(0)

#define MK_RADIO_IFNAME(idx)    do { \
                                    snprintf(ARRAY_AND_SIZE(buf), "radio.%d", idx); \
                                    ifname = buf; \
                                } while(0)

#define BREAK_IF_NOT_MGR(x)     if (sync_mgr != SYNC_MGR_##x) \
                                    break; \
                                else \
                                    LOGD("Sync Received message '%s' from Mesh-Agent", \
                                         sync_message_name(mp->msgType))

    switch (mp->msgType)
    {

        case MESH_WIFI_SSID_NAME:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiSSIDName.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_SSID_NAME: cannot get ApName");
                break;
            }

            LOGI("... %s changed SSID to '%s'", ssid_ifname, mp->data.wifiSSIDName.ssid);

            if (!vif_external_ssid_update(mp->data.wifiSSIDName.ssid, mp->data.wifiSSIDName.index))
            {
                LOGE("Cannot update config table for SSID: %s", mp->data.wifiSSIDName.ssid);
            }
            break;

        case MESH_WIFI_AP_SECURITY:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiAPSecurity.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_AP_SECURITY: cannot get ApName");
                break;
            }
            LOGI("... %s changed Security to: pass '%s', sec '%s', enc '%s'",
                    ssid_ifname,
                    mp->data.wifiAPSecurity.passphrase,
                    mp->data.wifiAPSecurity.secMode,
                    mp->data.wifiAPSecurity.encryptMode);
            if (!vif_external_security_update(mp->data.wifiSSIDName.index, mp->data.wifiAPSecurity.passphrase,
                        mp->data.wifiAPSecurity.secMode))
            {
                LOGE("Cannot update config table for SSID: %s", mp->data.wifiSSIDName.ssid);
            }
            break;

        case MESH_WIFI_AP_ADD_ACL_DEVICE:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiAPAddAclDevice.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_AP_ADD_ACL_DEVICE:: cannot get ApName");
                break;
            }
            LOGI("... %s added '%s' to ACL", ssid_ifname, mp->data.wifiAPAddAclDevice.mac);
            break;

        case MESH_WIFI_AP_DEL_ACL_DEVICE:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiAPDelAclDevice.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_AP_DEL_ACL_DEVICE: cannot get ApName");
                break;
            }
            LOGI("... %s deleted '%s' from ACL", ssid_ifname, mp->data.wifiAPDelAclDevice.mac);
            break;

        case MESH_WIFI_MAC_ADDR_CONTROL_MODE:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiMacAddrControlMode.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_MAC_ADDR_CONTROL_MODE: cannot get ApName");
                break;
            }
            LOGI("... %s ACL mode change, enable '%s', blacklist '%s'",
                    ssid_ifname,
                    mp->data.wifiMacAddrControlMode.isEnabled ? "true" : "false",
                    mp->data.wifiMacAddrControlMode.isBlacklist ? "true" : "false");
            break;

        case MESH_WIFI_SSID_ADVERTISE:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiSSIDAdvertise.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_SSID_ADVERTISE: cannot get ApName");
                break;
            }
            LOGI("... %s SSID advertise now '%s'",
                    ssid_ifname,
                    mp->data.wifiSSIDAdvertise.enable ? "true" : "false");
            break;

        case MESH_URL_CHANGE:
            BREAK_IF_NOT_MGR(CM);
            LOGI("... Backhaul URL changed to '%s'", mp->data.url.url);
            target_managers_restart();
            break;

        case MESH_WIFI_RESET:
            BREAK_IF_NOT_MGR(WM);
            LOGI("... Wifi Reset '%s'", mp->data.wifiReset.reset ? "true" : "false");
            break;

        case MESH_SUBNET_CHANGE:
            BREAK_IF_NOT_MGR(NM);
            LOGI("... Subnet change, gwIP '%s', nmask '%s'", mp->data.subnet.gwIP, mp->data.subnet.netmask);
            target_managers_restart();
            break;

        case MESH_WIFI_AP_KICK_ALL_ASSOC_DEVICES:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiAPKickAllAssocDevices.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_AP_KICK_ALL_ASSOC_DEVICES: cannot get ApName");
                break;
            }
            LOGI("... %s: Kick all devices", ssid_ifname);
            break;

        case MESH_WIFI_AP_KICK_ASSOC_DEVICE:
            BREAK_IF_NOT_MGR(WM);
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(mp->data.wifiAPKickAssocDevice.index, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("MESH_WIFI_AP_KICK_ASSOC_DEVICE: cannot get ApName");
                break;
            }
            LOGI("... %s: Kick device '%s'", ssid_ifname, mp->data.wifiAPKickAssocDevice.mac);
            break;

        case MESH_WIFI_RADIO_CHANNEL:
            BREAK_IF_NOT_MGR(WM);
            radioIndex = mp->data.wifiRadioChannel.index;
            memset(radio_ifname, 0, sizeof(radio_ifname));
            ret = wifi_getRadioIfName(radioIndex, radio_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("%s: cannot get radio ifname for idx %d", __func__, radioIndex);
                break;
            }
            LOGI("... %s: changed channel to %d", radio_ifname, mp->data.wifiRadioChannel.channel);
            break;

        case MESH_WIFI_RADIO_CHANNEL_MODE:
            BREAK_IF_NOT_MGR(WM);
            radioIndex = mp->data.wifiRadioChannelMode.index;
            memset(radio_ifname, 0, sizeof(radio_ifname));
            ret = wifi_getRadioIfName(radioIndex, radio_ifname);
            if (ret != RETURN_OK)
            {
                LOGE("%s: cannot get radio ifname for idx %d", __func__, radioIndex);
                break;
            }
            LOGI("... %s: changed mode to '%s', gOnly '%s', nOnly '%s', acOnly '%s'",
                    radio_ifname,
                    mp->data.wifiRadioChannelMode.channelMode,
                    mp->data.wifiRadioChannelMode.gOnlyFlag ? "true" : "false",
                    mp->data.wifiRadioChannelMode.nOnlyFlag ? "true" : "false",
                    mp->data.wifiRadioChannelMode.acOnlyFlag ? "true" : "false");
            break;

        case MESH_STATE_CHANGE:
            BREAK_IF_NOT_MGR(CM);
            LOGI("... Updating cloud mode from mesh state");
            if (mp->data.meshState.state == MESH_STATE_FULL)
            {
                cloud_mode = RADIO_CLOUD_MODE_FULL;
            }
            else
            {
                cloud_mode = RADIO_CLOUD_MODE_MONITOR;
            }

            current_cloud_mode = radio_cloud_mode_get();
            if (current_cloud_mode == cloud_mode)
            {
                break;
            }

            switch (cloud_mode)
            {
                case RADIO_CLOUD_MODE_FULL:
                    cloud_config_set_mode(SCHEMA_CONSTS_DEVICE_MODE_CLOUD);
                    break;
                case RADIO_CLOUD_MODE_MONITOR:
                    cloud_config_set_mode(SCHEMA_CONSTS_DEVICE_MODE_MONITOR);
                    break;
                default:
                    LOGW("Failed to set device mode! :: unknown value = %d", cloud_mode);
                    break;
            }

            if (!radio_cloud_mode_set(cloud_mode))
            {
                LOGE("Failed to set new cloud mode %d", cloud_mode);
            }
            break;

        case MESH_CLIENT_CONNECT:
            BREAK_IF_NOT_MGR(NM);
            LOGD("... %s client '%s' (%s) %sconnected",
                    sync_iface_name(mp->data.meshConnect.iface),
                    mp->data.meshConnect.mac,
                    mp->data.meshConnect.host,
                    mp->data.meshConnect.isConnected ? "" : "dis");

            if ((mltype = wiifhal_sync_iface_mltype(mp->data.meshConnect.iface)) >= 0)
            {
                maclearn_update(
                        mltype,
                        mp->data.meshConnect.mac,
                        mp->data.meshConnect.isConnected);
            }
            break;

        case MESH_DHCP_RESYNC_LEASES:
            BREAK_IF_NOT_MGR(NM);
            LOGD("... Resyncing DHCP leases");

            resync_leases();
            break;

        case MESH_DHCP_UPDATE_LEASE:
            BREAK_IF_NOT_MGR(NM);
            LOGD("... Update DHCP lease: mac=\"%s\", ipaddr=\"%s\", hn=\"%s\", fp=\"%s\"",
                    mp->data.meshLease.mac,
                    mp->data.meshLease.ipaddr,
                    mp->data.meshLease.hostname,
                    mp->data.meshLease.fingerprint);

            resync_leases();
            break;

        case MESH_DHCP_ADD_LEASE:
            BREAK_IF_NOT_MGR(NM);
            LOGD("... Add DHCP lease: mac=\"%s\", ipaddr=\"%s\", hn=\"%s\", fp=\"%s\"",
                    mp->data.meshLease.mac,
                    mp->data.meshLease.ipaddr,
                    mp->data.meshLease.hostname,
                    mp->data.meshLease.fingerprint);

            resync_leases();
            break;

        case MESH_DHCP_REMOVE_LEASE:
            BREAK_IF_NOT_MGR(NM);
            LOGD("... Remove DHCP lease: mac=\"%s\", ipaddr=\"%s\", hn=\"%s\", fp=\"%s\"",
                    mp->data.meshLease.mac,
                    mp->data.meshLease.ipaddr,
                    mp->data.meshLease.hostname,
                    mp->data.meshLease.fingerprint);

            resync_leases();
            break;

        default:
            // Only announce "unknown/unsupported" messages in WM
            BREAK_IF_NOT_MGR(WM);
            break;

    }
#undef MK_SSID_IFNAME
#undef MK_RADIO_IFNAME
    if (sync_mgr == SYNC_MGR_WM)
    {
        radio_trigger_resync();
    }

    return;
}

static void sync_evio_cb(struct ev_loop *loop, ev_io *watcher, int revents)
{
    MeshSync            mmsg;
    int                 ret;

    if (revents & EV_ERROR)
    {
        LOGE("Sync client MSGQ reported a socket error, reconnecting...");
        sync_reconnect();
        return;
    }

    ret = recv(sync_fd, &mmsg, sizeof(mmsg), MSG_DONTWAIT);
    if (ret <= 0)
    {
        LOGE("Sync client failed to read message, errno %d, reconnecting...", errno);
        sync_reconnect();
        return;
    }
    else if (ret != sizeof(mmsg))
    {
        LOGE("Sync client received malformed packet (length %d != %d)", ret, (int)sizeof(mmsg));
        return;
    }

    if (mmsg.msgType >= MESH_SYNC_MSG_TOTAL)
    {
        LOGE("Sync client received unsupported msg type (%d >= %d)",
             mmsg.msgType, MESH_SYNC_MSG_TOTAL);
        return;
    }

    sync_process_msg(&mmsg);
    return;
}

static bool sync_send_msg(MeshSync *mp)
{
    int ret;

    if (sync_fd < 0)
    {
        LOGE("Sync could not send message type \"%s\", not connected...",
             sync_message_name(mp->msgType));
        return false;
    }

    ret = send(sync_fd, (void *)mp, sizeof(*mp), MSG_DONTWAIT);
    if (ret <= 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            LOGW("Sync client was too busy to send message type \"%s\", dropped...",
                 sync_message_name(mp->msgType));
        }
        else
        {
            LOGE("Sync client failed to send message type \"%s\", errno = %d",
                 sync_message_name(mp->msgType), errno);
        }

        return false;
    }

    return true;
}

static void sync_task_connect(struct ev_loop *loop, ev_timer *timer_ptr, int revents)
{
    struct sockaddr_un  addr;
    const char          *uds_path = MESH_SOCKET_PATH_NAME;
    int                 ret;
    int                 fd;

    // Setup address of socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    if (*uds_path == '\0')
    {
        // Abstract socket name
        *addr.sun_path = '\0';
        strscpy(addr.sun_path+1, uds_path+1, sizeof(addr.sun_path)-1);
    }
    else
    {
        // Socket name on file system
        STRSCPY(addr.sun_path, uds_path);
    }

    // Create socket
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        LOGE("Sync client failed to connect -- socket creation failure, errno = %d", errno);
        return;
    }

    // Connect to Mesh-Agent
    ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        LOGE("Sync client failed to connect, errno = %d", errno);
        close(fd);
        return;
    }

    // Add to libev
    ev_io_init(&sync_evio, sync_evio_cb, fd, EV_READ);
    ev_io_start(wifihal_evloop, &sync_evio);

    LOGI("Sync client connected to Mesh-Agent (fd %d)", fd);
    sync_fd = fd;

    if (sync_on_connect_cb)
    {
        sync_on_connect_cb();
    }

    ev_timer_stop(loop, timer_ptr);

    return;
}

static void sync_disconnect(void)
{
    // Cancel any connection tasks
    ev_timer_stop(EV_DEFAULT_ &sync_timer);

    // Nothing else to do if not connected
    if (sync_fd < 0)
    {
        return;
    }

    // Stop EVIO and disconnect
    ev_io_stop(wifihal_evloop, &sync_evio);
    close(sync_fd);
    sync_fd = -1;

    LOGI("Sync client disconnected from Mesh-Agent");
    return;
}

static void sync_reconnect(void)
{
    // Disconnect current socket if one exists
    sync_disconnect();

    // Schedule task to connect
    ev_timer_init(&sync_timer, sync_task_connect, 0, SYNC_RETRY);
    ev_timer_again(EV_DEFAULT_ &sync_timer);

    return;
}

/*****************************************************************************/

void sync_init(sync_mgr_t mgr, sync_on_connect_cb_t sync_cb)
{
    if (sync_initialized)
    {
        return;
    }
    sync_on_connect_cb = sync_cb;

    // Kick of connect task
    sync_reconnect();

    LOGN("Sync client initialized");
    sync_initialized = true;
    sync_mgr = mgr;
}

bool sync_cleanup(void)
{
    if (!sync_initialized)
    {
        return true;
    }

    sync_disconnect();

    LOGN("Sync client cleaned up");
    sync_initialized = false;

    return true;
}

bool sync_send_ssid_change(
        INT ssid_index,
        const char *ssid_ifname,
        const char *new_ssid)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("%s: Cannot send SSID update -- sync not initialized", ssid_ifname);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_SSID_NAME;
    mmsg.data.wifiSSIDName.index = ssid_index;
    STRSCPY(mmsg.data.wifiSSIDName.ssid, new_ssid);

    if (!sync_send_msg(&mmsg))
    {
        return false;
    }

    LOGN("%s: MSGQ sent SSID update to '%s'", ssid_ifname, new_ssid);
    return true;
}

bool sync_send_security_change(
        INT ssid_index,
        const char *ssid_ifname,
        MeshWifiAPSecurity *sec)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("%s: Cannot send security update -- sync not initialized",
             ssid_ifname);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_AP_SECURITY;
    memcpy(&mmsg.data.wifiAPSecurity, sec, sizeof(mmsg.data.wifiAPSecurity));
    mmsg.data.wifiAPSecurity.index = ssid_index;

    if (!sync_send_msg(&mmsg))
    {
        // It reports error
        return false;
    }

    LOGN("%s: MSGQ sent security update (sec '%s', enc '%s')",
         ssid_ifname, sec->secMode, sec->encryptMode);
    return true;
}

bool sync_send_status(radio_cloud_mode_t mode)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("Cannot send cloud mode status update -- sync not initialized");
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));
    mmsg.msgType = MESH_WIFI_STATUS;

    switch (mode)
    {
    default:
    case RADIO_CLOUD_MODE_UNKNOWN:
        LOGI("Not sending cloud mode status update -- mode is unknown");
        return true;

    case RADIO_CLOUD_MODE_MONITOR:
        mmsg.data.wifiStatus.status = MESH_WIFI_STATUS_MONITOR;
        break;

    case RADIO_CLOUD_MODE_FULL:
        mmsg.data.wifiStatus.status = MESH_WIFI_STATUS_FULL;
        break;
    }
    if (!sync_send_msg(&mmsg))
    {
        // It reports error
        return false;
    }

    LOGN("MSGQ sent cloud mode status update (mode %d)\n",
         mmsg.data.wifiStatus.status);
    return true;
}

bool sync_send_channel_change(
        INT radio_index,
        UINT channel)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("%d: Cannot send channel update -- sync not initialized", radio_index);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_RADIO_CHANNEL;
    mmsg.data.wifiRadioChannel.index = radio_index;
    mmsg.data.wifiRadioChannel.channel = channel;

    if (!sync_send_msg(&mmsg))
    {
        return false;
    }

    LOGN("%d: MSGQ sent channel update to %u", radio_index, channel);
    return true;
}

bool sync_send_ssid_broadcast_change(
        INT ssid_index,
        BOOL ssid_broadcast)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("%d: Cannot SSID Broadcast update -- sync not initialized", ssid_index);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_SSID_ADVERTISE;
    mmsg.data.wifiSSIDAdvertise.index = ssid_index;
    mmsg.data.wifiSSIDAdvertise.enable = ssid_broadcast;

    if (!sync_send_msg(&mmsg))
    {
        return false;
    }

    LOGN("%d: MSGQ sent SSID broadcast update to %s", ssid_index, (ssid_broadcast ? "true" : "false"));
    return true;
}

bool sync_send_channel_bw_change(
        INT ssid_index,
        UINT bandwidth)
{
    MeshSync        mmsg;

    if (!sync_initialized)
    {
        LOGW("%d: Cannot channel bandwidth update -- sync not initialized", ssid_index);
        return false;
    }

    memset(&mmsg, 0, sizeof(mmsg));

    mmsg.msgType = MESH_WIFI_RADIO_CHANNEL_BW;
    mmsg.data.wifiRadioChannelBw.index = ssid_index;
    mmsg.data.wifiRadioChannelBw.bw = bandwidth;

    if (!sync_send_msg(&mmsg))
    {
        return false;
    }

    LOGN("%d: MSGQ sent channel bandwidth update to %u MHz", ssid_index, bandwidth);
    return true;
}
