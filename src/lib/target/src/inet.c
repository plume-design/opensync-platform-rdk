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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <ev.h>

#include <errno.h>

#include <sys/socket.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "os.h"
#include "log.h"
#include "os_nif.h"
#include "os_types.h"
#include "util.h"
#include "os_util.h"
#include "os_regex.h"
#include "target.h"
#include "schema.h"
#include "ds_dlist.h"
#include "ds_tree.h"

#include "wifihal.h"
#include "devinfo.h"
#include "const.h"


#define MODULE_ID LOG_MODULE_ID_OSA

// TEMP: What bridge to use for GRE downlinks when cloud doesn't support VLANs yet
#define CLOUD_NO_VLAN_BRIDGE    "br-home"

#define MAX_SIPADDR_SIZE    16  // String length of a string representing an IP address with terminator


/******************************************************************************
 *  INET definitions
 *****************************************************************************/

#define MAX_DEVNAME_SIZE                    256
#define TARGET_VIF_INET_STATE_PERIOD        10.0     // 300ms polling period

typedef void inet_state_fn(
                struct schema_Wifi_Inet_State *state,
                schema_filter_t *filter);

/**
 * Network interface status
 */
struct inet
{
    char                            in_ifname[MAX_DEVNAME_SIZE];
    struct schema_Wifi_Inet_Config  in_config;
    struct schema_Wifi_Inet_State   in_state;
    inet_state_fn                  *in_callback;
    ds_tree_node_t                  in_node;

    // Interface type specific stuff
    union
    {
        struct
        {
            bool created;
        } in_gre;

        struct
        {
            bool created;
        } in_vlan;
    };
};

static ev_timer inet_state_timer;

/**
 * Target network interface list -- indexed by interface name
 */
static ds_tree_t inet_iface_list = DS_TREE_INIT((ds_key_cmp_t *)strcmp,
                                                struct inet,
                                                in_node);

static void inet_state_periodic(
        struct ev_loop *loop,
        ev_timer *w,
        int revents);

static int inet_ioctl_socket = -1;

static struct inet* inet_fetch(const char *ifname, char *iftype);

bool inet_prepopulate_from_devinfo(
        char *ifname,
        char *devinfo_ip,
        char *devinfo_mac)
{
    char dbuf[256];

    struct inet *nif;

    // Allocate pre-populated br-wan interface
    nif = inet_fetch(ifname, "bridge");

    // Set the type to bridge
    STRLCPY(nif->in_config.if_type, "bridge");
    STRLCPY(nif->in_state.if_type, "bridge");

    // Set these to TRUE, we don't wanna shut down the WAN interface by accident
    nif->in_config.enabled = true;
    nif->in_state.enabled = true;

    nif->in_config.network = true;
    nif->in_state.network = true;

    // Populate the IP address
    if (!devinfo_getv(devinfo_ip, dbuf, sizeof(dbuf), false))
    {
        LOGW("INET: Prepopulation of IP address for %s failed. Using defaults.", ifname);
        nif->in_state.inet_addr_exists = false;
        nif->in_config.inet_addr_exists = false;

        nif->in_state.inet_addr[0] = '\0';
        nif->in_config.inet_addr[0] = '\0';
    }
    else
    {
        nif->in_state.inet_addr_exists = true;
        nif->in_config.inet_addr_exists = true;

        STRLCPY(nif->in_state.inet_addr, dbuf);
        STRLCPY(nif->in_config.inet_addr, dbuf);
    }

    // Populate the MAC address
    if (!devinfo_getv(devinfo_mac, dbuf, sizeof(dbuf), false))
    {
        LOGW("INET: Prepopulation of MAC address for %s failed. Using defaults.", ifname);
        snprintf(nif->in_state.hwaddr, sizeof(nif->in_state.hwaddr), "00:00:00:00:00:00");
    }
    else
    {
        STRLCPY(nif->in_state.hwaddr, dbuf);
    }

    nif->in_state.hwaddr_exists = true;

    return true;
}

/**
 * Initialize the INET subsystem
 *
 * XXX: What does VIF mean
 */
bool target_vif_inet_init(void)
{
    // Open an AF_INET socket -- this will be used for ioctl()s only
    inet_ioctl_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (inet_ioctl_socket < 0)
    {
        LOGE("Unable to open socket for INET IOCTL operations (state).");
        return false;
    }

    // Pre-populate br-wan
    if (!inet_prepopulate_from_devinfo("br-wan", DEVINFO_WAN_IP, DEVINFO_WAN_MAC))
    {
        LOGW("Unable to prepopulate the br-wan interface.");
    }

    // Pre-populate br-home
    if (!inet_prepopulate_from_devinfo("br-home", DEVINFO_HOME_IP, DEVINFO_HOME_MAC))
    {
        LOGE("Unable to prepopulate the br-home interface.");
        return false;
    }

    // Start the Inet State polling timer
    ev_timer_init(
            &inet_state_timer,
            inet_state_periodic,
            TARGET_VIF_INET_STATE_PERIOD,
            TARGET_VIF_INET_STATE_PERIOD);

    ev_timer_start(EV_DEFAULT, &inet_state_timer);

    return true;
}


/**
 * Find or allocate a new Inet State element structure
 */
static struct inet* inet_fetch(const char *ifname, char *iftype)
{
    struct inet *nif;

    nif = ds_tree_find(&inet_iface_list, (void *)ifname);
    if (nif != NULL)
    {
        // Element already exists -- return it
        return nif;
    }

    // Unable to create the interface if the type is not specified
    if (iftype == NULL) return NULL;

    // Create a new element
    nif = calloc(1, sizeof(*nif));
    STRLCPY(nif->in_ifname, ifname);
    STRLCPY(nif->in_config.if_name, ifname);
    STRLCPY(nif->in_state.if_name, ifname);

    STRLCPY(nif->in_config.if_type, iftype);
    STRLCPY(nif->in_state.if_type, iftype);

    ds_tree_insert(
            &inet_iface_list,
            nif,
            nif->in_ifname);

    return nif;
}

/**
 * Update the @p state structure with the current interface state as acquired from the system
 */
static bool inet_state_refresh(
        char *ifname,
        struct schema_Wifi_Inet_State *state)
{
    struct ifreq req;
    int rc;

    STRLCPY(req.ifr_name, ifname);

    // Verify if the interface exists. If it doesn't abort any operation and return.
    // XXX: In such cases do not modify the Wifi_Inet_State structure.
    rc = ioctl(inet_ioctl_socket, SIOCGIFINDEX, &req);
    if (rc != 0)
    {
        // Silent error, this may be happening a lot
        LOGT("Unable to acquire state, interface does not exist: %s", ifname);
        return false;
    }

    // Get the IP address
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;
    rc = ioctl(inet_ioctl_socket, SIOCGIFADDR, &req);
    if (rc != 0 && errno != EADDRNOTAVAIL)
    {
        LOGE("Error acquiring IP on interface %s (rc = %d): %s.", ifname, rc, strerror(errno));
        return false;
    }

    // This is not an error. SIOCGIFADDR returns EADDRNOTAVAIL when an address
    // is not set on the interface. In such cases we must return 0.0.0.0
    if (errno == EADDRNOTAVAIL)
    {
        state->inet_addr[0] = '\0';
        state->inet_addr_exists = false;
    }
    else
    {
        // Convert IP to string
        STRLCPY(state->inet_addr, inet_ntoa(((struct sockaddr_in *)&req.ifr_addr)->sin_addr));
        state->inet_addr_exists = true;
    }

    // Get the NETMASK
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;
    rc = ioctl(inet_ioctl_socket, SIOCGIFNETMASK, &req);
    if (rc != 0 && errno != EADDRNOTAVAIL)
    {
        LOGE("Error acquiring NETMASK on interface %s.", ifname);
        return false;
    }

    // Same reason as for SIOCGIFADDR, see above
    if (errno == EADDRNOTAVAIL)
    {
        state->netmask[0] = '\0';
        state->netmask_exists = false;
    }
    else
    {
        // Convert netmask to string
        STRLCPY(state->netmask, inet_ntoa(((struct sockaddr_in *)&req.ifr_netmask)->sin_addr));
        state->netmask_exists = true;
    }

    // Get the broadcast address
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;
    rc = ioctl(inet_ioctl_socket, SIOCGIFBRDADDR, &req);
    if (rc != 0 && errno != EADDRNOTAVAIL)
    {
        LOGE("Error acquiring BRDADDR on interface %s.", ifname);
        return false;
    }

    // Same reason as for SIOCGIFADDR, see above
    if (errno == EADDRNOTAVAIL)
    {
        state->broadcast[0] = '\0';
        state->broadcast_exists = false;
    }
    else
    {
        // Convert broadcast to string
        STRLCPY(state->broadcast, inet_ntoa(((struct sockaddr_in *)&req.ifr_broadaddr)->sin_addr));
        state->broadcast_exists = true;
    }

    // Get the MTU
    rc = ioctl(inet_ioctl_socket, SIOCGIFMTU, &req);
    if (rc >= 0)
    {
        state->mtu = req.ifr_mtu;
        state->mtu_exists = true;
    }
    else
    {
        state->mtu_exists = false;
        state->mtu = 0;
    }

    return true;
}

/**
 * Periodic timer for Inet status update
 */
static void inet_state_periodic(
        struct ev_loop *loop,
        ev_timer *w,
        int revents)
{
    struct inet *nif;

    ds_tree_foreach(&inet_iface_list, nif)
    {
        struct schema_Wifi_Inet_State old;

        memcpy(&old, &nif->in_state, sizeof(old));

        if (!inet_state_refresh(nif->in_ifname, &nif->in_state))
        {
            // Silence this, it might get really verbose when an interface doesn't exist.
            continue;
        }

        // Compare new and old structures, call callback if any of the relevant fields changed
        bool change_detected = false;

        // Compare inet_addr
        if (old.inet_addr_exists != nif->in_state.inet_addr_exists)
        {
            change_detected = true;
        }
        else if (old.inet_addr_exists && strcmp(old.inet_addr, nif->in_state.inet_addr) != 0)
        {
            change_detected = true;
        }

        // Compare network
        if (old.netmask_exists != nif->in_state.netmask_exists)
        {
            change_detected = true;
        }
        else if (old.netmask_exists && strcmp(old.netmask, nif->in_state.netmask) != 0)
        {
            change_detected = true;
        }

        // Compare broadcast
        if (old.broadcast_exists != nif->in_state.broadcast_exists)
        {
            change_detected = true;
        }
        else if (old.broadcast_exists && strcmp(old.broadcast, nif->in_state.broadcast) != 0)
        {
            change_detected = true;
        }

        if (old.mtu_exists != nif->in_state.mtu_exists)
        {
            change_detected = true;
        }
        else if (old.mtu_exists && old.mtu != nif->in_state.mtu)
        {
            change_detected = true;
        }

        // No change detected, continue
        if (!change_detected) continue;

        // Process the change
        LOGI("Interface change detected on: %s. New state: IP:%s NETMASK:%s BRDADDR:%s MTU:%d",
                nif->in_ifname,
                nif->in_state.inet_addr_exists ? nif->in_state.inet_addr : "(none)",
                nif->in_state.netmask_exists ? nif->in_state.netmask : "(none)",
                nif->in_state.broadcast_exists ? nif->in_state.broadcast : "(none)",
                nif->in_state.mtu_exists ? nif->in_state.mtu : -1);

        if (nif->in_callback != NULL)
        {
            LOGD("Calling callback.");
            nif->in_callback(&nif->in_state, NULL);
        }
        else
        {
            LOGD("No callback registered.");
        }
    }
}

/**
 * Return the PID of the udhcpc client serving on interface @p ifname
 *
 * This function returns 0 if DHCPC client is not running.
 */
static int inet_dhcpc_pid(char *ifname)
{
    char pid_file[256];
    FILE *f;
    int pid;
    int rc;

    snprintf(pid_file, sizeof(pid_file), "/var/run/udhcpc-%s.pid", ifname);

    f = fopen(pid_file, "r");
    if (f == NULL) return 0;

    rc = fscanf(f, "%d", &pid);
    fclose(f);

    // We should read exactly one element
    if (rc != 1)
    {
        return 0;
    }

    if (kill(pid, 0) != 0)
    {
        return 0;
    }

    return pid;
}

/**
 * Start the DHCP service for interface @p ifname
 */
static bool inet_dhcpc_start(char *ifname)
{
    char pidfile[256];
    pid_t pid;
    int status;

    pid = inet_dhcpc_pid(ifname);
    if (pid > 0)
    {
        LOG(ERR, "DHCP client already running::ifname=%s", ifname);
        return true;
    }

    snprintf(pidfile, sizeof(pidfile), "/var/run/udhcpc-%s.pid", ifname);

    // Double fork -- disown the process
    pid = fork();
    if (pid == 0)
    {
        if (fork() == 0)
        {
            char *argv[] =
            {
                "/sbin/udhcpc",
                "-p", pidfile,
                "-s", "/opt/we/bin/udhcpc.sh",
                "-b",
                "-t", "60",
                "-T", "1",
                "-o",
                "-O", "1",
                "-O", "3",
                "-O", "6",
                "-O", "12",
                "-O", "15",
                "-O", "28",
                "-O", "43",
                "-i", ifname,
                "-S",
                NULL
            };

            execv("/sbin/udhcpc", argv);
        }
        exit(0);
    }

    // Wait for the first child -- it should exit immediately
    waitpid(pid, &status, 0);

    return true;
}

/**
 * Stop the DHCP service on interface @p ifname
 */
bool inet_dhcpc_stop(char *ifname)
{
    int pid = inet_dhcpc_pid(ifname);
    if (pid <= 0)
    {
        LOG(DEBUG, "DHCP client not running::ifname=%s", ifname);
        return true;
    }

    int signum = SIGTERM;
    int tries = 0;

    while (kill(pid, signum) == 0)
    {
        if (tries++ > 20)
        {
            signum = SIGKILL;
        }

        usleep(100*1000);
    }

    return true;
}


static bool inet_config_apply_none(struct inet *nif)
{
    struct ifreq req;
    int rc;

    // None scheme means there's no assigned IP -- set it to 0.0.0.0
    STRLCPY(req.ifr_name, nif->in_ifname);
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;

    rc = ioctl(inet_ioctl_socket, SIOCSIFADDR, &req);
    if (rc != 0)
    {
        LOGE("Error setting assignment scheme to 'none' on interface: %s.", nif->in_ifname);
        return false;
    }

    return true;
}

static bool inet_config_apply_static(struct inet *nif)
{
    struct ifreq req;
    int rc;

    struct schema_Wifi_Inet_Config *conf = &nif->in_config;

    STRLCPY(req.ifr_name, nif->in_ifname);

    LOGI("Assigning static ip to interface %s: %s/%s(brd %s)",
         nif->in_ifname,
         conf->inet_addr,
         conf->netmask,
         conf->broadcast_exists ? conf->broadcast : "N/A");

    // Set the IP address
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;
    rc = inet_aton(conf->inet_addr, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
    if (rc != 1)
    {
        LOGE("Invalid IP address: %s", conf->inet_addr);
        return false;
    }

    rc = ioctl(inet_ioctl_socket, SIOCSIFADDR, &req);
    if (rc != 0)
    {
        LOGE("Error setting assignment scheme to 'none' on interface: %s.",
             nif->in_ifname);
        return false;
    }

    // Set the netmask
    memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
    req.ifr_addr.sa_family = AF_INET;
    rc = inet_aton(conf->netmask, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
    if (rc != 1)
    {
        LOGE("Invalid netmask address: %s", conf->inet_addr);
        return false;
    }

    rc = ioctl(inet_ioctl_socket, SIOCSIFNETMASK, &req);
    if (rc != 0)
    {
        LOGE("Error setting assignment scheme to 'none' on interface: %s.",
             nif->in_ifname);
        return false;
    }

    // Set the broadcast address
    if (conf->broadcast_exists)
    {
        memset(&req.ifr_addr, 0, sizeof(req.ifr_addr));
        req.ifr_addr.sa_family = AF_INET;
        rc = inet_aton(conf->broadcast, &((struct sockaddr_in *)&req.ifr_addr)->sin_addr);
        if (rc != 1)
        {
            LOGE("Invalid netmask address: %s", conf->inet_addr);
            return false;
        }

        rc = ioctl(inet_ioctl_socket, SIOCSIFBRDADDR, &req);
        if (rc != 0)
        {
            LOGE("Error setting assignment scheme to 'none' on interface: %s.",
                 nif->in_ifname);
            return false;
        }
    }

    return true;
}

static bool inet_config_apply_ip_assign_scheme(struct inet *nif)
{
    struct schema_Wifi_Inet_Config *conf = &nif->in_config;

    // Treat network == false as ip_assign_scheme == "none"
    // Also, no ip_assign_scheme is treated as none
    if (   !conf->ip_assign_scheme_exists
        || strlen(conf->ip_assign_scheme) == 0
        || strcmp(conf->ip_assign_scheme, "none") == 0
        || !conf->network)
    {
        return inet_config_apply_none(nif);
    }
    else if (strcmp(conf->ip_assign_scheme, "static") == 0)
    {
        if (conf->inet_addr_exists && conf->netmask_exists)
        {
            return inet_config_apply_static(nif);
        }
        else
        {
            LOGE("Static scheme requires inet_addr and netmask.");
        }
    }
    else if (strcmp(conf->ip_assign_scheme, "dhcp") == 0)
    {
        // This is handled by inet_config_apply_dhcpc()
        return true;
    }
    else
    {
        LOGW("%s: Unknown DHCP scheme: \"%s\"", conf->if_name, conf->ip_assign_scheme);
    }

    LOGE("Forcing \"none\" assignment scheme to interface: %s\n", nif->in_ifname);
    return inet_config_apply_none(nif);
}

static bool inet_config_apply_dhcpc(struct inet *nif)
{
    bool is_running;
    bool should_run;

    struct schema_Wifi_Inet_Config *conf = &nif->in_config;

    // inet_dhcpc_pid() returns non-zero if the DHCPC client is already running
    is_running = (inet_dhcpc_pid(nif->in_ifname) != 0);

    // DHCPC must be enabled if ip_assign_scheme == dhcp and if interface and network is enabled
    should_run = (strcmp(conf->ip_assign_scheme, "dhcp") == 0) && conf->enabled && conf->network;

    if (is_running == should_run) return true;

    if (should_run)
    {
        LOGI("Starting DHCP client services on interface: %s", nif->in_ifname);
        // Clear any IP settings currently on the interface -- inet_config_apply_none() happens
        // to do just that
        if (!inet_config_apply_none(nif))
        {
            LOGE("Error revoking current IP settings on interface: %s", nif->in_ifname);
            return false;
        }

        if (!inet_dhcpc_start(nif->in_ifname))
        {
            LOGE("Error starting DHCPC on interface: %s", nif->in_ifname);
            return false;
        }
    }
    else
    {
        LOGI("Stopping DHCP client services on interface: %s", nif->in_ifname);
        if (!inet_dhcpc_stop(nif->in_ifname))
        {
            LOGE("Error stopping DHCPC on interface: %s", nif->in_ifname);
            return false;
        }
    }

    return true;
}

static bool inet_config_apply(struct inet *nif)
{
    struct ifreq req;
    int rc;

    struct schema_Wifi_Inet_Config *conf = &nif->in_config;

    STRLCPY(req.ifr_name, nif->in_ifname);

    // Check that interface exists by getting the interface index
    rc = ioctl(inet_ioctl_socket, SIOCGIFINDEX, &req);
    if (rc < 0)
    {
        // Silent error, this may be happening a lot
        LOGD("Interface %s does not exists, unable to set config.", nif->in_ifname);
        return false;
    }

    // Set the MTU
    if (conf->mtu_exists)
    {
        req.ifr_mtu = conf->mtu;
        rc = ioctl(inet_ioctl_socket, SIOCGIFMTU, &req);
        if (rc != 0)
        {
            LOGE("Error setting MTU on interface %s.", nif->in_ifname);
            return false;
        }
    }

    // Get the current interface flags
    rc = ioctl(inet_ioctl_socket, SIOCGIFFLAGS, &req);
    if (rc != 0)
    {
        LOGE("Error retrieving interface status: %s", nif->in_ifname);
        return false;
    }

    if (conf->enabled)
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
        LOGE("Error setting interface status: %s", nif->in_ifname);
    }

    // If the interface is disabled, we're done
    if (!conf->enabled)
    {
        return true;
    }

    // DHCPC settings
    if (!inet_config_apply_dhcpc(nif))
    {
        LOGE("Error applying DHCPC config.");
        return false;
    }

    // Set the IP address
    if (!inet_config_apply_ip_assign_scheme(nif))
    {
        LOGE("Error setting assignment scheme.");
        return false;
    }

    // XXX: DHCPD / DNS
    if (conf->NAT_exists && conf->NAT)
    {
        LOGE("NAT not supported.");
        conf->NAT = false;
    }

    if (conf->upnp_mode_exists && strcmp(conf->upnp_mode, "none") != 0)
    {
        LOGE("UPnP not supported.");
        STRLCPY(conf->upnp_mode, "none");
    }

    if (conf->dhcp_sniff_exists && conf->dhcp_sniff)
    {
        LOGE("DHCP fingerprinting not supported,");
        conf->dhcp_sniff = false;
    }

    return true;
}

bool inet_config_set(
        struct inet *nif,
        struct schema_Wifi_Inet_Config *iconf)
{
    // Cache the current config
    memcpy(&nif->in_config, iconf, sizeof(nif->in_config));

    // Update state by copying some of the config fields

    if (!inet_config_apply(nif))
    {
        LOGD("Error applying config for interface: %s", nif->in_ifname);
        return false;
    }

    nif->in_state.network = iconf->network;
    nif->in_state.enabled = iconf->enabled;

    STRLCPY(nif->in_state.ip_assign_scheme, iconf->ip_assign_scheme);
    nif->in_state.ip_assign_scheme_exists = iconf->ip_assign_scheme_exists;

    nif->in_state.NAT = iconf->NAT;
    nif->in_state.NAT_exists = iconf->NAT_exists;

    STRLCPY(nif->in_state.gateway, iconf->gateway);
    nif->in_state.gateway_exists = iconf->gateway_exists;

    STRLCPY(nif->in_state.gre_ifname, iconf->gre_ifname);
    nif->in_state.gre_ifname_exists = iconf->gre_ifname_exists;

    STRLCPY(nif->in_state.gre_remote_inet_addr, iconf->gre_remote_inet_addr);
    nif->in_state.gre_remote_inet_addr_exists = iconf->gre_remote_inet_addr_exists;

    STRLCPY(nif->in_state.gre_local_inet_addr, iconf->gre_local_inet_addr);
    nif->in_state.gre_local_inet_addr_exists = iconf->gre_local_inet_addr_exists;

    STRLCPY(nif->in_state.upnp_mode, iconf->upnp_mode);
    nif->in_state.upnp_mode_exists = iconf->upnp_mode_exists;

    STRLCPY(nif->in_state.parent_ifname, iconf->parent_ifname);
    nif->in_state.parent_ifname_exists = iconf->parent_ifname_exists;

    nif->in_state.vlan_id = iconf->vlan_id;
    nif->in_state.vlan_id_exists = iconf->vlan_id_exists;

    if (   sizeof(nif->in_state.dns) != sizeof(iconf->dns)
        || sizeof(nif->in_state.dns_keys) != sizeof(iconf->dns_keys))
    {
        LOGE("State dns map and Config dns map out of sync. Please update the schema.");
        return false;
    }

    memcpy(nif->in_state.dns_keys, iconf->dns_keys, sizeof(nif->in_state.dns_keys));
    memcpy(nif->in_state.dns, iconf->dns, sizeof(nif->in_state.dns));
    nif->in_state.dns_len = iconf->dns_len;

    if (   sizeof(nif->in_state.dhcpd) != sizeof(iconf->dhcpd)
        || sizeof(nif->in_state.dhcpd_keys) != sizeof(iconf->dhcpd_keys))
    {
        LOGE("State dhcpd map and Config dhcpd map out of sync. Please update the schema.");
        return false;
    }

    memcpy(nif->in_state.dhcpd, iconf->dhcpd, sizeof(nif->in_state.dhcpd));
    memcpy(nif->in_state.dhcpd_keys, iconf->dhcpd_keys, sizeof(nif->in_state.dhcpd_keys));
    nif->in_state.dhcpd_len = iconf->dhcpd_len;

    return true;
}


/******************************************************************************
 *  INET PUBLIC API
 *****************************************************************************/

static char *inet_init_iflist[] =
{
    "br-wan",
    "br-home",
    NULL
};

bool
target_inet_config_init(ds_dlist_t *inets)
{
    target_inet_config_init_t *init;
    struct inet *nif;
    char **piface;

    ds_dlist_init(inets, target_inet_config_init_t, dsl_node);

    for (piface = inet_init_iflist; *piface != NULL; piface++)
    {
        nif = inet_fetch(*piface, NULL);
        if (nif == NULL)
        {
            LOGW("INET: Interface %s not initialized, cannot populate config.", *piface);
            continue;
        }

        init = calloc(1, sizeof(target_inet_config_init_t));
        if (init == NULL)
        {
            LOGW("INET: Error allocating init config structures for %s, cannot populate config.", *piface);
            continue;
        }

        memcpy(&init->iconfig, &nif->in_config, sizeof(init->iconfig));

        // Add interface to the list
        ds_dlist_insert_tail(inets, init);
    }

    return true;
}


bool
target_inet_state_init(ds_dlist_t *inets)
{
    target_inet_state_init_t *init;
    struct inet *nif;
    char **piface;

    ds_dlist_init(inets, target_inet_state_init_t, dsl_node);

    for (piface = inet_init_iflist; *piface != NULL; piface++)
    {
        nif = inet_fetch(*piface, NULL);
        if (nif == NULL)
        {
            LOGW("INET: Interface %s not initialized, cannot populate state.", *piface);
            return false;
        }

        init = calloc(1, sizeof(target_inet_state_init_t));
        if (init == NULL)
        {
            LOGW("INET: Error allocating init config structures for %s, cannot populate state.", *piface);
            return false;
        }

        memcpy(&init->istate, &nif->in_state, sizeof(init->istate));

        // Add interface to the list
        ds_dlist_insert_tail(inets, init);
    }

    return true;
}

bool
target_vif_inet_config_set(
        char                           *ifname,
        struct schema_Wifi_Inet_Config *iconf)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "vif");
    if (nif == NULL)
    {
        LOGE("Error allocating interface %s.", ifname);
        return false;
    }

    return inet_config_set(nif, iconf);
}

bool
target_vif_inet_state_get(
        char                           *ifname,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "vif");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    memcpy(istate, &nif->in_state, sizeof(*istate));

    return true;
}

bool
target_tap_inet_config_set(
        char                           *ifname,
        struct schema_Wifi_Inet_Config *iconf)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "tap");
    if (nif == NULL)
    {
        LOGE("Error allocating interface %s.", ifname);
        return false;
    }

    return inet_config_set(nif, iconf);
}

bool
target_tap_inet_state_get(
        char                           *ifname,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "tap");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    memcpy(istate, &nif->in_state, sizeof(*istate));

    return true;
}

bool
target_gre_inet_config_set(
        char                           *ifname,
        char                           *remote_ip,
        struct schema_Wifi_Inet_Config *iconf)
{
    struct inet *nif;
    char *gre_parent;
    char *bridge;

    nif = inet_fetch(iconf->if_name, "gre");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    // First create the interface (if it doesn't already exist)
    if (!nif->in_gre.created)
    {
        int  rc;
        char cmd[256];

        // Check that all required settings are present
        if (   !iconf->gre_local_inet_addr_exists
            || !iconf->gre_remote_inet_addr_exists
            || !iconf->gre_ifname_exists)
        {
            LOGE("%s: Unable to create GRE interface. Missing local/remote IP or parent interface.",
                 iconf->if_name);
            return false;
        }

        if (!(gre_parent = target_map_ifname_to_gre_bridge(iconf->gre_ifname)))
        {
            gre_parent = target_map_ifname(iconf->gre_ifname);
        }

        // Check that cloud is a version that supports VLANs. The cloud supporting VLANS
        // now sets parent_ifname the same as gre_ifname, for transition purposes.
        if (!iconf->parent_ifname_exists || strlen(iconf->parent_ifname) == 0)
        {
            bridge = (char *)CLOUD_NO_VLAN_BRIDGE;
        }
        else
        {
            if (!(bridge = target_map_ifname_to_bridge(iconf->gre_ifname)))
            {
                LOGE("%s: Not creating GRE interface, bridge lookup failed for %s",
                     iconf->if_name, iconf->gre_ifname);
                return false;
            }
        }

        // XXX: Remove interface first
        snprintf(cmd, sizeof(cmd), "ip link del %s > /dev/null 2>&1", iconf->if_name);
        // In most cases the command above will fail, be silent about it
        (void)system(cmd);

        snprintf(cmd, sizeof(cmd),
                "ip link add %s type gretap"
                " local %s"
                " remote %s"
                " dev %s"
                " tos 1",
                iconf->if_name,
                iconf->gre_local_inet_addr,
                iconf->gre_remote_inet_addr,
                gre_parent);

        LOGD("%s: Creating with \"%s\"", iconf->if_name, cmd);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
        {
            LOGE("%s: Failed to create GRE interface \"%s\", rc = %d", iconf->if_name, cmd, WEXITSTATUS(rc));
            return false;
        }

        snprintf(cmd, sizeof(cmd), "brctl addif %s %s", bridge, iconf->if_name);
        LOGD("%s: Adding to bridge with \"%s\"", iconf->if_name, cmd);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
        {
            LOGE("%s: Failed to add GRE to bridge \"%s\", rc = %d",
                 iconf->if_name, cmd, WEXITSTATUS(rc));
            return false;
        }

        nif->in_gre.created = true;
    }

    // Apply inet config
    return inet_config_set(nif, iconf);
}


bool
target_gre_inet_state_get(
        char                           *ifname,
        char                           *remote_ip,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    // XXX: NM2 uses the parents interface name here -- that's kind of useless for us.
    // Look up the proper interface name and use that!
    ds_tree_foreach(&inet_iface_list, nif)
    {
        if (strcmp(nif->in_config.if_type, "gre") != 0) continue;
        if (strcmp(nif->in_config.gre_ifname, ifname) != 0) continue;
        if (!nif->in_config.gre_remote_inet_addr_exists) continue;
        if (strcmp(nif->in_config.gre_remote_inet_addr, remote_ip) != 0) continue;

        memcpy(istate, &nif->in_state, sizeof(*istate));

        return true;  /* SUCCESS */
    }

    LOGE("GRE interface with parent interface %s not found.", ifname);
    return false;
}

bool
target_vlan_inet_config_set(
        char                           *ifname,
        struct schema_Wifi_Inet_Config *iconf)
{
    struct inet *nif;
    char *bridge;

    nif = inet_fetch(iconf->if_name, "vlan");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    // First create the interface (if it doesn't already exist)
    if (!nif->in_vlan.created)
    {
        int  rc;
        char cmd[256];

        // Check that all required settings are present
        if (!iconf->parent_ifname_exists || !iconf->vlan_id_exists)
        {
            LOGE("%s: Unable to create VLAN interface. Missing parent interface or VLAN ID.",
                 iconf->if_name);
            return false;
        }

        snprintf(cmd, sizeof(cmd), "%s.%u", iconf->parent_ifname, iconf->vlan_id);
        if (strcmp(cmd, iconf->if_name) != 0)
        {
            LOGE("%s: Not creating VLAN interface, if_name doesn't match \"%s\"",
                 iconf->if_name, cmd);
            return false;
        }

        if (!inet_fetch(iconf->parent_ifname, NULL))
        {
            LOGE("%s: Not creating VLAN interface, Parent \"%s\" doesn't exist",
                 iconf->if_name, iconf->parent_ifname);
            return false;
        }

        if (!(bridge = target_map_vlan_to_bridge(iconf->vlan_id)))
        {
            LOGE("%s: Not creating VLAN interface, bridge lookup failed for VLAN %u",
                 iconf->if_name, iconf->vlan_id);
            return false;
        }

        // XXX: Remove interface first
        snprintf(cmd, sizeof(cmd), "vconfig rem %s > /dev/null 2>&1", iconf->if_name);
        (void)system(cmd);
        // Return value not checked.
        // In most cases running the above command will fail, so be silent about it.

        snprintf(cmd, sizeof(cmd),
                "vconfig add %s %u",
                iconf->parent_ifname,
                iconf->vlan_id);

        LOGD("%s: Creating with \"%s\"", iconf->if_name, cmd);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
        {
            LOGE("%s: Failed to create VLAN interface \"%s\", rc = %d",
                 iconf->if_name, cmd, WEXITSTATUS(rc));
            return false;
        }

        snprintf(cmd, sizeof(cmd), "brctl addif %s %s", bridge, iconf->if_name);
        LOGD("%s: Adding to bridge with \"%s\"", iconf->if_name, cmd);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
        {
            LOGE("%s: Failed to add VLAN to bridge \"%s\", rc = %d",
                 iconf->if_name, cmd, WEXITSTATUS(rc));
            return false;
        }

        nif->in_vlan.created = true;
    }

    // Apply inet config
    return inet_config_set(nif, iconf);
}


bool
target_vlan_inet_state_get(
        char                           *ifname,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "vlan");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    memcpy(istate, &nif->in_state, sizeof(*istate));

    return true;
}

bool
target_bridge_inet_state_get(char *ifname, struct schema_Wifi_Inet_State *istate)
{
    char *devinfo_ip = NULL;
    char *devinfo_mac = NULL;

    if (strcmp(ifname, "br-home") == 0)
    {
        devinfo_ip = DEVINFO_HOME_IP;
        devinfo_mac = DEVINFO_HOME_MAC;
    }
    else if (strcmp(ifname, "br-wan") == 0)
    {
        devinfo_ip = DEVINFO_WAN_IP;
        devinfo_mac = DEVINFO_WAN_MAC;
    }
    else
    {
        LOGE("Unable to resolve interface: %s.", ifname);
        return false;
    }

    if (!inet_prepopulate_from_devinfo(ifname, devinfo_ip, devinfo_mac))
    {
        LOGE("Unable to prepopulate interface: %s.", ifname);
        return false;
    }

    return true;
}


bool
target_eth_inet_state_get(
        const char                     *ifname,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "eth");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    memcpy(istate, &nif->in_state, sizeof(*istate));

    return true;
}

bool
target_ppp_inet_state_get(
        char                           *ifname,
        struct schema_Wifi_Inet_State  *istate)
{
    struct inet *nif;

    nif = inet_fetch(ifname, "bridge");
    if (nif == NULL)
    {
        LOGE("Unable to fetch INET structure for interface %s.", ifname);
        return false;
    }

    memcpy(istate, &nif->in_state, sizeof(*istate));

    return true;
}

bool
target_inet_state_register(
        char                           *ifname,
        void                           *istate_cb)
{
    struct inet *nif;

    nif = inet_fetch(ifname, NULL);

    if (nif == NULL) return false;

    nif->in_callback = istate_cb;

    return true;
}

bool
target_mac_learning_register(void *maclearn_cb)
{
    return wifihal_maclearn_init(maclearn_cb);
}
