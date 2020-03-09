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

#ifndef OSYNC_HAL_H_INCLUDED
#define OSYNC_HAL_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

/**
 * @file osync_hal.h
 * @brief OpenSync Hardware Abstraction Layer.
 *
 * 'OSync HAL' abstracts hardware / system-level functions of the target
 * that are not (yet) included in RDK API. Some of the APIs from this file
 * may become a part of RDK (or similar) HAL API in the future.
 *
 * All the functions must be non-blocking.
 */

/**
 * @brief OSync HAL return code.
 */
typedef enum {
    OSYNC_HAL_SUCCESS = 0,
    OSYNC_HAL_FAILURE
} osync_hal_return_t;

/**
 * The length of MAC address in ASCII string format.
 *  "AA:BB:CC:DD:EE:FF" -> 17 characters plus NULL terminator.
 */
#define MAC_ADDR_STR_LEN 18

/**
 * The maximum length of IP address in ASCII string format.
 *  "255.255.255.255" -> 15 characters plus NULL terminator.
 */
#define IP_ADDR_STR_LEN 16

/**
 * The maximum supported length of DHCP fingerprint data.
 */
#define FINGERPRINT_STR_LEN 128

/**
 * The maximum supported length of hostname.
 */
#define HOSTNAME_STR_LEN 128

/**
 * @brief Types of network interfaces.
 *
 *  Supported main types of network interfaces.
 */
typedef enum {
    OSYNC_HAL_IFACE_NONE = 0,
    OSYNC_HAL_IFACE_ETHERNET,
    OSYNC_HAL_IFACE_MOCA,
    OSYNC_HAL_IFACE_WIFI,
    OSYNC_HAL_IFACE_OTHER
} osync_hal_iface_type_t;

/**
 * @brief Client description.
 */
typedef struct {
    osync_hal_iface_type_t iface;    /**< Client's type of interface */
    char mac_str[MAC_ADDR_STR_LEN];  /**< MAC in ASCII string format */
} osync_hal_clients_info_t;

/**
 * @brief Cloud modes.
 */
typedef enum
{
    OSYNC_HAL_DEVINFO_CLOUD_MODE_UNKNOWN  = 0,  /**< Mode is unknown */
    OSYNC_HAL_DEVINFO_CLOUD_MODE_MONITOR  = 1,  /**< OpenSync only reads statistics,
                                                     no state changes are issued
                                                     from cloud controller */
    OSYNC_HAL_DEVINFO_CLOUD_MODE_FULL     = 2,  /**< Full (normal) control mode */
} osync_hal_devinfo_cloud_mode_t;

/**
 * @brief Interface configuration.
 */
typedef struct {
    bool enabled;
    char inet_addr[IP_ADDR_STR_LEN];
    char netmask[IP_ADDR_STR_LEN];
    char broadcast[IP_ADDR_STR_LEN];
    unsigned int mtu;
    char mac_str[MAC_ADDR_STR_LEN];
} osync_hal_inet_iface_config_t;

/**
 * @brief Client's DHCP lease data.
 */
typedef struct {
    char mac_str[MAC_ADDR_STR_LEN];         /**< MAC in ASCII string format */
    char ip_str[IP_ADDR_STR_LEN];           /**< IP in ASCII string format */
    char hostname[HOSTNAME_STR_LEN];        /**< Client's hostname */
    char fingerprint[FINGERPRINT_STR_LEN];  /**< Client's DHCP fingerprint */
    unsigned int lease_time;  /**< How long the lease is valid in seconds.
                                   Value "0" means the lease is not valid
                                   anymore and its entry should be removed. */
} osync_hal_dhcp_lease_t;

/**
 * @brief Initialize OSync HAL
 *
 * Perform any low-level, vendor-specific initialization.
 * This is a very first OSync HAL function called by OpenSync.
 * If no initialization is required just return success.
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 *
 */
osync_hal_return_t osync_hal_init(void);


/**
 * @brief Deinitialize OSync HAL
 *
 * Perform vendor-specific deinitialization.
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 *
 */
osync_hal_return_t osync_hal_deinit(void);

/**
 * @brief Check if target is ready to start OpenSync.
 *
 * This function will be polled on start by OpenSync until
 * vendor-specific prerequisites are met.
 *
 * Example prerequisite can be a datetime set or network interface
 * status.
 * If there are no vendor-specific conditions to check, simple return
 * OSYNC_HAL_SUCCESS.
 *
 * @return OSYNC_HAL_SUCCESS if target is ready, OSYNC_HAL_FAILURE
 *         otherwise.
 */
osync_hal_return_t osync_hal_ready(void);

/**
 * @brief Callback called for each connected client.
 *
 * This callback is a non-blocking function called during fetching
 * of all already connected clients.
 *
 */
typedef void (*osync_hal_handle_client_fn)(const osync_hal_clients_info_t *clients_info);

/**
 * @brief Fetch list of connected clients
 *
 * Calls handle_client_fn() for each already connected client.
 * The handle_client_fn() must be called directly inside this function (cannot
 * be deferred or cached). Must be called from the same context (thread) as
 * the osync_hal_fetch_connected_clients().
 *
 * Note: This function is meant to be called just after registering with the
 * Mesh Agent to ensure that we get information about all the clients that were
 * connected before the Mesh Agent was started. After that, the Mesh Agent will
 * send notifications about new clients.
 *
 * @param handle_client_fn callback
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_fetch_connected_clients(
        osync_hal_handle_client_fn
        handle_client_fn);

/**
 * @brief Get interface configuration.
 *
 * Get IP and other related information for given network interface.
 *
 * @param[in] if_name name of the interface
 * @param[out] config interface configuration
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_get_iface_config(
        const char *if_name,
        osync_hal_inet_iface_config_t *config);

/**
 * @brief Set interface configuration.
 *
 * Set IP and other related information for given network interface.
 * If one of the fields (for example netmask) is not known, set it to
 * '\0'.
 *
 * @param[in] if_name name of the interface
 * @param[in] config requested interface configuration
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_set_iface_config(
        const char *if_name,
        const osync_hal_inet_iface_config_t *config);

/**
 * @brief Create GRE tunnel.
 *
 * Create new Generic Routing Encapsulation TAP interface.
 * If the interface already exists, it should be updated.
 *
 * @param[in] if_name name of the new interface
 * @param[in] local IP address of local endpoint as ASCII string
 * @param[in] remote IP address of remote endpoint as ASCII string
 * @param[in] dev name of the parent interface
 * @param[in] tos type of service
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_create_gre(
        const char *if_name,
        const char *local,
        const char *remote,
        const char *dev,
        unsigned int tos);

/**
 * @brief Destroy GRE tunnel.
 *
 * Destroy Generic Routing Encapsulation TAP interface.
 *
 * @param[in] if_name name of interface to destroy
 *
 * @return OSYNC_HAL_SUCCESS if interface was successfully destroyed
 *         or does not exist, OSYNC_HAL_FAILURE otherwise.
 */
osync_hal_return_t osync_hal_inet_destroy_gre(const char *if_name);

/**
 * @brief Add interface to the bridge.
 *
 * @param[in] if_name name of interface
 * @param[in] br_name name of the bridge
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_add_to_bridge(
        const char *if_name,
        const char *br_name);

/**
 * @brief Create VLAN interface.
 *
 * Create virtual interface with VLAN id.
 *
 * @param[in] if_name name of interface
 * @param[in] vlan_id VLAN ID
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_create_vlan(
        const char *if_name,
        unsigned int vlan_id);

/**
 * @brief Destroy VLAN interface.
 *
 * Destroy virtual interface with VLAN id.
 *
 * @param[in] if_name name of interface
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_inet_destroy_vlan(const char *if_name);

/**
 * @brief Callback called on DHCP leases change.
 *
 * This callback is a non-blocking function called when DHCP leases
 * are changed.
 * Note, if lease should be removed the lease_time should be set to "0".
 *
 * @return true on success false otherwise.
 */
typedef bool (*osync_hal_dhcp_lease_fn)(const osync_hal_dhcp_lease_t *dhcp_lease);

/**
 * @brief Synchronize all DHCP leases
 *
 * Calls dhcp_lease_fn() registered by osync_hal_dhcp_lease_register() for each
 * DHCP lease entry.
 * The dhcp_lease_fn() must be called directly inside this function (cannot
 * be deferred or cached). Must be called from the same context (thread) as
 * the osync_hal_dhcp_resync_all().
 *
 * @param dhcp_lease_fn function to be called for every dhcp lease entry
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_dhcp_resync_all(osync_hal_dhcp_lease_fn dhcp_lease_fn);

/**
 * @brief Get current cloud mode.
 *
 * The OpenSync can run in several control modes. This function
 * returns mode which should be used.
 *
 * @param[out] mode requested control mode
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_devinfo_get_cloud_mode(osync_hal_devinfo_cloud_mode_t *mode);

/**
 * @brief Get redirector address.
 *
 * The OpenSync needs a cloud redirector address. This function provides
 * that address. The address is an ASCII string in a format:
 * "ssl:<hostname>:<port>", for example: "ssl:wildfire.plume.tech:443".
 *
 * @param[out] buf buffer to which the redirector address will be written
 * @param[in]  bufsz size of the buf. In most cases 64 bytes should be enough
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_devinfo_get_redirector_addr(
        char *buf,
        size_t bufsz);

/**
 * @brief Get country code.
 *
 * Return the 802.11d regulatory domain country code which is used by
 * given interface. The country code must be 2 capital letters ASCII string.
 *
 * @param[in]  if_name name of the radio interface
 * @param[out] buf buffer to which the country code will be written
 *                 Example output: "EU" or "US"
 * @param[in]  bufsz size of the buf
 *
 * @return OSYNC_HAL_SUCCESS or OSYNC_HAL_FAILURE.
 */
osync_hal_return_t osync_hal_get_country_code(
        const char *if_name,
        char *buf,
        size_t bufsz);

#endif /* OSYNC_HAL_H_INCLUDED */
