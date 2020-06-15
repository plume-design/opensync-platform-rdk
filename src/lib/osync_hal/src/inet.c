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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "log.h"
#include "util.h"
#include "os_util.h"
#include "osync_hal.h"
#include "devinfo.h"


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_GET_IFACE_CONFIG

#define DEVINFO_CACHED_SIZE 64

osync_hal_return_t osync_hal_inet_get_iface_config(
        const char *if_name,
        osync_hal_inet_iface_config_t *config)
{
    static char home_ip_cached[DEVINFO_CACHED_SIZE];
    static char home_mac_cached[DEVINFO_CACHED_SIZE];
    static char wan_ip_cached[DEVINFO_CACHED_SIZE];
    static char wan_mac_cached[DEVINFO_CACHED_SIZE];

    static bool once = true;

    /*
     * On RDK we only handle interfaces accessed via deviceinfo.sh.
     * Other interfaces are handled directly by NM via standard Linux
     * networking tools.
     * Calls to deviceinfo.sh may take significant amount of time,
     * so we cache ip and mac for optimization purposes.
     * We also assume the WAN IP and MAC will not be changed without
     * restarting OpenSync.
     */
    if (once)
    {
        devinfo_getv(DEVINFO_HOME_IP,  home_ip_cached,  sizeof(home_ip_cached));
        devinfo_getv(DEVINFO_HOME_MAC, home_mac_cached, sizeof(home_mac_cached));
        devinfo_getv(DEVINFO_WAN_IP,   wan_ip_cached,   sizeof(wan_ip_cached));
        devinfo_getv(DEVINFO_WAN_MAC,  wan_mac_cached,  sizeof(wan_mac_cached));

        once = false;
    }

    if (!strcmp(if_name, CONFIG_RDK_LAN_BRIDGE_NAME))
    {
        STRSCPY(config->inet_addr, home_ip_cached);
        STRSCPY(config->mac_str, home_mac_cached);
    }
    else if (!strcmp(if_name, CONFIG_RDK_WAN_BRIDGE_NAME))
    {
        STRSCPY(config->inet_addr, wan_ip_cached);
        STRSCPY(config->mac_str, wan_mac_cached);
    }
    else
    {
        LOGE("Cannot get config for %s. Only %s and %s interfaces available", if_name,
                CONFIG_RDK_LAN_BRIDGE_NAME, CONFIG_RDK_WAN_BRIDGE_NAME);
        return OSYNC_HAL_FAILURE;
    }


    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_GET_IFACE_CONFIG */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_SET_IFACE_CONFIG
osync_hal_return_t osync_hal_inet_set_iface_config(
        const char *if_name,
        const osync_hal_inet_iface_config_t *config)
{
    int inet_ioctl_socket;
    struct ifreq req;
    int rc;
    int ret = OSYNC_HAL_FAILURE;

    inet_ioctl_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (inet_ioctl_socket < 0)
    {
        LOGE("Unable to open socket for INET IOCTL operations (state).");
        return OSYNC_HAL_FAILURE;
    }

    STRLCPY(req.ifr_name, if_name);

    // Check that interface exists by getting the interface index
    rc = ioctl(inet_ioctl_socket, SIOCGIFINDEX, &req);
    if (rc < 0)
    {
        // Silent error, this may be happening a lot
        LOGD("Interface %s does not exists, unable to set config.", if_name);
        goto out;
    }

    LOGI("Assigning static ip to interface %s: %s/%s(brd %s)",
         if_name,
         config->inet_addr,
         config->netmask,
         config->broadcast);

    // Get the current interface flags
    rc = ioctl(inet_ioctl_socket, SIOCGIFFLAGS, &req);
    if (rc != 0)
    {
        LOGE("Error retrieving interface status: %s", if_name);
        goto out;
    }

    if (config->enabled)
    {
        req.ifr_flags |= IFF_UP;
    }
    else
    {
        req.ifr_flags &= ~IFF_UP;
    }

    // Set interface status
    rc = ioctl(inet_ioctl_socket, SIOCSIFFLAGS, &req);
    if (rc != 0)
    {
        LOGE("Error setting interface status: %s", if_name);
        goto out;
    }

    if (!config->enabled)
    {
        ret = OSYNC_HAL_SUCCESS;
        goto out;
    }

    if (strlen(config->inet_addr) > 0)
    {
        // Set the IP address
        memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
        req.ifr_addr.sa_family = AF_INET;
        rc = inet_aton(config->inet_addr, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
        if (rc != 1)
        {
            LOGE("Invalid IP address: %s", config->inet_addr);
            goto out;
        }

        rc = ioctl(inet_ioctl_socket, SIOCSIFADDR, &req);
        if (rc != 0)
        {
            LOGE("Error setting static IP assignment scheme on interface: %s.",
                  if_name);
            goto out;
        }
    }

    if (strlen(config->netmask) > 0)
    {
        // Set the netmask
        memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
        req.ifr_addr.sa_family = AF_INET;
        rc = inet_aton(config->netmask, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
        if (rc != 1)
        {
            LOGE("Invalid netmask address: %s", config->netmask);
            goto out;
        }

        rc = ioctl(inet_ioctl_socket, SIOCSIFNETMASK, &req);
        if (rc != 0)
        {
            LOGE("Error setting netmask on interface: %s.",
                  if_name);
            goto out;
        }
    }

    // Set the broadcast address
    if (strlen(config->broadcast) > 0)
    {
        memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
        req.ifr_addr.sa_family = AF_INET;
        rc = inet_aton(config->broadcast, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
        if (rc != 1)
        {
            LOGE("Invalid broadcast address: %s", config->broadcast);
            goto out;
        }

        rc = ioctl(inet_ioctl_socket, SIOCSIFBRDADDR, &req);
        if (rc != 0)
        {
            LOGE("Error setting broadcast on interface: %s.",
                 if_name);
            goto out;
        }
    }

    // Set the MTU
    if (config->mtu != 0)
    {
        req.ifr_mtu = config->mtu;
        rc = ioctl(inet_ioctl_socket, SIOCGIFMTU, &req);
        if (rc != 0)
        {
            LOGE("Error setting MTU on interface %s.", if_name);
            goto out;
        }
    }

    ret = OSYNC_HAL_SUCCESS;

out:
    close(inet_ioctl_socket);
    return ret;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_SET_IFACE_CONFIG */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_CREATE_GRE
osync_hal_return_t osync_hal_inet_create_gre(
        const char *if_name,
        const char *local,
        const char *remote,
        const char *dev,
        unsigned int tos)
{
    char cmd[256];
    int rc;

    if (osync_hal_inet_destroy_gre(if_name) != OSYNC_HAL_SUCCESS)
    {
        return OSYNC_HAL_FAILURE;
    }

    snprintf(cmd, sizeof(cmd),
            "ip link add %s type gretap"
            " local %s"
            " remote %s"
            " dev %s"
            " tos %u",
            if_name,
            local,
            remote,
            dev,
            tos);

    LOGD("%s: Creating with \"%s\"", if_name, cmd);
    rc = system(cmd);
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        LOGE("%s: Failed to create GRE interface \"%s\", rc = %d", if_name, cmd, WEXITSTATUS(rc));
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_CREATE_GRE */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_DESTROY_GRE
osync_hal_return_t osync_hal_inet_destroy_gre(const char *if_name)
{
    char cmd[256];

    memset(cmd, 0, sizeof(cmd));

    snprintf(cmd, sizeof(cmd), "ip link del %s > /dev/null 2>&1", if_name);

    LOGD("%s: Destroying with \"%s\"", if_name, cmd);
    // In most cases the command above will fail, be silent about it
    (void)system(cmd);

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_DESTROY_GRE */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_ADD_TO_BRIDGE
static osync_hal_return_t osync_hal_is_ovs_bridge(const char *br_name)
{
    char cmd[256];
    int rc;

    snprintf(cmd, sizeof(cmd), "ovs-vsctl br-exists %s", br_name);
    LOGD("%s: Checking bridge if ovs with \"%s\"", br_name, cmd);
    rc = system(cmd);
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
        return OSYNC_HAL_FAILURE;

    return OSYNC_HAL_SUCCESS;
}

osync_hal_return_t osync_hal_inet_add_to_bridge(
        const char *if_name,
        const char *br_name)
{
    char cmd[256];
    int rc;

    if (osync_hal_is_ovs_bridge(br_name) == OSYNC_HAL_SUCCESS)
    {
        // If bridge is ovs bridge then do nothing.
        // Cloud controller will insert interface into the bridge.
        return OSYNC_HAL_SUCCESS;
    }

    memset(cmd, 0, sizeof(cmd));

    snprintf(cmd, sizeof(cmd), "brctl addif %s %s", br_name, if_name);
    LOGD("%s: Adding to bridge with \"%s\"", if_name, cmd);
    rc = system(cmd);
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        LOGE("%s: Failed to add GRE to bridge \"%s\", rc = %d",
        if_name, cmd, WEXITSTATUS(rc));
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_ADD_TO_BRIDGE */


#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_CREATE_VLAN
osync_hal_return_t osync_hal_inet_create_vlan(
        const char *if_name,
        unsigned int vlan_id)
{
    char cmd[256];
    int rc;

    memset(cmd, 0, sizeof(cmd));

    // XXX: Remove interface first
    snprintf(cmd, sizeof(cmd), "vconfig rem %s.%u > /dev/null 2>&1", if_name, vlan_id);
    (void)system(cmd);
    // Return value not checked.
    // In most cases running the above command will fail, so be silent about it.

    snprintf(cmd, sizeof(cmd),
            "vconfig add %s %u",
            if_name,
            vlan_id);

    LOGD("%s: Creating with \"%s\"", if_name, cmd);
    rc = system(cmd);
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        LOGE("%s: Failed to create VLAN interface \"%s\", rc = %d",
             if_name, cmd, WEXITSTATUS(rc));
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_CREATE_VLAN */

#ifdef CONFIG_OSYNC_HAL_USE_DEFAULT_INET_DESTROY_VLAN
osync_hal_return_t osync_hal_inet_destroy_vlan(const char *if_name)
{
    char cmd[256];
    int rc;

    memset(cmd, 0, sizeof(cmd));

    // XXX: Remove interface first
    snprintf(cmd, sizeof(cmd), "vconfig rem %s > /dev/null 2>&1", if_name);
    LOGD("%s: Destroying with \"%s\"", if_name, cmd);
    rc = system(cmd);
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        LOGE("%s: Failed to destroy VLAN interface \"%s\", rc = %d",
                if_name, cmd, WEXITSTATUS(rc));
        return OSYNC_HAL_FAILURE;
    }

    return OSYNC_HAL_SUCCESS;
}
#endif /* CONFIG_OSYNC_HAL_USE_DEFAULT_INET_DESTROY_VLAN */
