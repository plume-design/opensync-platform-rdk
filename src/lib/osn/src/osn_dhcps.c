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
 * ===========================================================================
 *  Provides the implementation for the following OpenSync APIs:
 *      - DHCPv4 server
 * ===========================================================================
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>

#include "ds_dlist.h"
#include "ds_tree.h"
#include "log.h"
#include "evx.h"
#include "util.h"
#include "daemon.h"
#include "os_file.h"
#include "os_regex.h"

#include "osn_dhcp.h"
#include "kconfig.h"

#include "target.h"
#include "target_internal.h"

/*
 * ===========================================================================
 * DHCPv4 Server
 * ===========================================================================
 */
/* Debounce timer, the maximum amount time to wait before initializing DHCP leases */
#define DHCP_LEASE_INIT_DEBOUNCE_TIMER     0.3

/* Number of elements to add to the lease array each time it is resized */
#define DHCP_LEASE_RESIZE_QUANTUM          16

struct osn_dhcp_server
{
    char                            ds_ifname[C_IFNAME_LEN];
    osn_dhcp_server_status_fn_t    *ds_status_fn;
    struct osn_dhcp_server_status   ds_status;
    ds_dlist_node_t                 ds_dnode;
    void                           *ds_data;

    /* IP pool range list */
    ds_tree_t                       ds_range_list;
};

/*
 * IP range structure
 */
struct dhcp_range
{
    osn_ip_addr_t                   dr_range_start;
    osn_ip_addr_t                   dr_range_stop;
    ds_tree_node_t                  dr_tnode;
};

/* IP range comparison function */
int dhcp_range_cmp(void *_a, void *_b)
{
    int rc;

    struct dhcp_range *a = _a;
    struct dhcp_range *b = _b;

    rc = osn_ip_addr_cmp(&a->dr_range_start, &b->dr_range_start);
    if (rc != 0) return rc;

    return osn_ip_addr_cmp(&a->dr_range_stop, &b->dr_range_stop);
}

/*
 * Static functions
 */
static bool               dhcp_server_init(osn_dhcp_server_t *self, const char *ifname);
static bool               lease_exists(struct osn_dhcp_server_status *st, struct osn_dhcp_server_lease *dl);
static bool               dhcp_server_lease_parse_line(struct osn_dhcp_server_lease *dl, const char *line);
static osn_dhcp_server_t* dhcp_server_find_by_lease(struct osn_dhcp_server_lease *dl);
static void               dhcp_server_lease_add(osn_dhcp_server_t *self, struct osn_dhcp_server_lease *dl);
static void               dhcp_lease_onchange(struct ev_loop *loop, ev_stat *w, int revent);
static void               dhcp_lease_init(struct ev_loop *loop, struct ev_debounce *ev, int revent);

/*
 * Globals
 */
static ds_dlist_t   dhcp_server_list = DS_DLIST_INIT(osn_dhcp_server_t, ds_dnode);
static ev_debounce  dhcp_lease_init_debounce;
static ev_stat      dhcp_lease_watcher;

/*
 * ===========================================================================
 *  OSync DHCPv4 Server API
 * ===========================================================================
 */
osn_dhcp_server_t* osn_dhcp_server_new(const char *ifname)
{
    osn_dhcp_server_t *self = calloc(1, sizeof(osn_dhcp_server_t));
    if (self == NULL)
    {
        LOGE("%s: Failed to allocate memory for new dhcp server", ifname);
        return NULL;
    }

    if (!dhcp_server_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

bool osn_dhcp_server_del(osn_dhcp_server_t *self)
{
    ds_tree_iter_t iter;
    struct dhcp_range *dr;

    /* Free DHCP range list */
    ds_tree_foreach_iter(&self->ds_range_list, dr, &iter)
    {
        ds_tree_iremove(&iter);
        free(dr);
    }

    /* Remove the DHCP server object instance from the global list */
    ds_dlist_remove(&dhcp_server_list, self);

    /* Stop the lease watcher and DHCP lease initialization */
    if (ev_is_active(&dhcp_lease_watcher) && ds_dlist_is_empty(&dhcp_server_list))
    {
        ev_stat_stop(EV_DEFAULT, &dhcp_lease_watcher);
        ev_debounce_stop(EV_DEFAULT, &dhcp_lease_init_debounce);
    }

    free(self);

    return true;
}

bool osn_dhcp_server_cfg_set(
        osn_dhcp_server_t *self,
        struct osn_dhcp_server_cfg *cfg)
{
    return true;
}

void osn_dhcp_server_data_set(
        osn_dhcp_server_t *self,
        void *data)
{
    self->ds_data = data;
}

void* osn_dhcp_server_data_get(osn_dhcp_server_t *self)
{
    return self->ds_data;
}

bool osn_dhcp_server_apply(osn_dhcp_server_t *self)
{
    return true;
}

bool osn_dhcp_server_range_add(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    struct dhcp_range *dr;

    struct dhcp_range rfind = { .dr_range_start = start, .dr_range_stop = stop };

    dr = ds_tree_find(&self->ds_range_list, &rfind);
    if (dr != NULL)
    {
        /* Range already exists */
        return true;
    }

    dr = calloc(1, sizeof(struct dhcp_range));
    if (dr == NULL)
    {
        LOGE("Failed to allocate memory for new dhcp range");
        return false;
    }
    dr->dr_range_start = start;
    dr->dr_range_stop = stop;
    ds_tree_insert(&self->ds_range_list, dr, dr);

    return true;
}

bool osn_dhcp_server_range_del(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    struct dhcp_range *dr;

    struct dhcp_range rfind = { .dr_range_start = start, .dr_range_stop = stop };

    dr = ds_tree_find(&self->ds_range_list, &rfind);
    if (dr == NULL) return true;

    ds_tree_remove(&self->ds_range_list, dr);

    free(dr);

    return true;
}

bool osn_dhcp_server_reservation_add(
        osn_dhcp_server_t *self,
        osn_mac_addr_t macaddr,
        osn_ip_addr_t ipaddr,
        const char *hostname)

{
    return true;
}

bool osn_dhcp_server_reservation_del(osn_dhcp_server_t *self, osn_mac_addr_t macaddr)
{
    return true;
}

void osn_dhcp_server_error_notify(osn_dhcp_server_t *self, osn_dhcp_server_error_fn_t *fn)
{
    return;
}

void osn_dhcp_server_status_notify(osn_dhcp_server_t *self, osn_dhcp_server_status_fn_t *fn)
{
    self->ds_status_fn = fn;
}

bool osn_dhcp_server_option_set(
        osn_dhcp_server_t *self,
        enum osn_dhcp_option opt,
        const char *value)
{
    return true;
}


/*
 * ===========================================================================
 *  DHCP Server Implementation
 * ===========================================================================
 */
static bool dhcp_server_init(osn_dhcp_server_t *self, const char *ifname)
{
    wifihal_evloop = EV_DEFAULT;

    /* Initialize this instance */
    memset(self, 0 ,sizeof(*self));

    if (strscpy(self->ds_ifname, ifname, sizeof(self->ds_ifname)) < 0)
    {
        LOG(ERR, "dhcpv4_server: Error initializing instance for %s, interface name too long.", ifname);
        return false;
    }

    /* Initialize IP range list */
    ds_tree_init(&self->ds_range_list, dhcp_range_cmp, struct dhcp_range, dr_tnode);

    if (ds_dlist_is_empty(&dhcp_server_list))
    {
        /* Initialize the DHCP leases file watcher */
        ev_stat_init(
            &dhcp_lease_watcher,
            dhcp_lease_onchange,
            CONFIG_RDK_DHCP_LEASES_PATH,
            0.0);

        ev_stat_start(EV_DEFAULT, &dhcp_lease_watcher);

        /* Trigger the update after the dhcp server instance is created */
        ev_debounce_init(&dhcp_lease_init_debounce, dhcp_lease_init, DHCP_LEASE_INIT_DEBOUNCE_TIMER);
        ev_debounce_start(EV_DEFAULT, &dhcp_lease_init_debounce);
    }

    /* Insert itself into the global list */
    ds_dlist_insert_tail(&dhcp_server_list, self);

    return true;
}

/*
 * Clear leases associated with the server instance -- lease information
 * is cached using the osn_dhcp_server_status structure
 */
static void dhcp_lease_clear(osn_dhcp_server_t *self)
{
    if (self->ds_status.ds_leases != NULL)
    {
        free(self->ds_status.ds_leases);
        self->ds_status.ds_leases = NULL;
    }

    self->ds_status.ds_leases_len = 0;
}

/*
 * Broadcast a status update to all DHCP server instances
 */
void dhcp_server_status_dispatch()
{
    osn_dhcp_server_t *ds;

    /*
     * The osn_dhcp_server_status structure is cached within the osn_dhcp_server_t structure.
     * Iterate the server list and call the notification callbacks.
     */
    ds_dlist_foreach(&dhcp_server_list, ds)
    {
        if (ds->ds_status_fn != NULL)
        {
            ds->ds_status_fn(ds, &ds->ds_status);
        }
    }
}

static bool dhcp_server_lease_parse_line(struct osn_dhcp_server_lease *dl, const char *line)
{
    /*
     * Regular expression to match a line in the "dhcp.lease" file of the
     * following format:
     *
     * 1461412276 f4:09:d8:89:54:4f 192.168.0.181 android-c992b284e24fdd69 1,33,3,6,15,28,51,58,59 "*" 01:f4:09:d8:89:54:4f
     */
    static const char dnsmasq_lease_parse_re[] =
        "(^[0-9]+) "                            /* 1: Match timestamp */
        "(([a-fA-F0-9]{2}:?){6}) "              /* 2,3: Match MAC address */
        "(([0-9]+\\.?){4}) "                    /* 4,5: Match IP address */
        "(\\*|[a-zA-Z0-9_-]+) "                 /* 6: Match hostname, can be "*" */
        "(\\*|([0-9]+,?)+) "                    /* 7,8: Match fingerprint, can be "*" */
        "\"(\\*|[^\"]+)\" "                     /* 9: Match vendor-class, can be "*" */
        "[^ ]+$";                               /* Match CID, can be "*" */

    static bool parse_re_compiled = false;
    static regex_t parse_re;

    char sleasetime[C_INT32_LEN];
    char shwaddr[C_MACADDR_LEN];
    char sipaddr[C_IP4ADDR_LEN];

    regmatch_t rm[10];

    if (!parse_re_compiled && regcomp(&parse_re, dnsmasq_lease_parse_re, REG_EXTENDED) != 0)
    {
        LOG(ERR, "dhcpv4_server: Error compiling DHCP lease parsing regular expression.");
        return false;
    }

    parse_re_compiled = true;

    /* Parse the lease line using regular expressions */
    if (regexec(&parse_re, line, ARRAY_LEN(rm), rm, 0) != 0)
    {
        LOG(ERR, "NM: Invalid DHCP lease line (ignoring): %s", line);
        return false;
    }

    memset(dl, 0, sizeof(*dl));

    /* Extract the data */
    os_reg_match_cpy(
            sleasetime,
            sizeof(sleasetime),
            line,
            rm[1]);

    os_reg_match_cpy(
            shwaddr,
            sizeof(shwaddr),
            line,
            rm[2]);

    os_reg_match_cpy(
            sipaddr,
            sizeof(sipaddr),
            line, rm[4]);

    os_reg_match_cpy(dl->dl_hostname,
            sizeof(dl->dl_hostname),
            line,
            rm[6]);

    /* Copy the fingerprint */
    os_reg_match_cpy(dl->dl_fingerprint,
            sizeof(dl->dl_fingerprint),
            line,
            rm[7]);

    /* Copy the vendor-class */
    os_reg_match_cpy(dl->dl_vendorclass,
            sizeof(dl->dl_vendorclass),
            line,
            rm[9]);

    dl->dl_leasetime = strtod(sleasetime, NULL);

    if (!osn_mac_addr_from_str(&dl->dl_hwaddr, shwaddr))
    {
        LOG(ERR, "dhcpv4_server: Invalid DHCP MAC address obtained from lease file: %s", shwaddr);
        return false;
    }

    if (!osn_ip_addr_from_str(&dl->dl_ipaddr, sipaddr))
    {
        LOG(ERR, "dhcpv4_server: Invalid DHCP IPv4 address obtained from lease file: %s", sipaddr);
        return false;
    }

    return true;
}

static bool lease_exists(struct osn_dhcp_server_status *st, struct osn_dhcp_server_lease *dl)
{
    int i;

    for (i = 0; i < st->ds_leases_len; i++)
    {
        /* We don't really check for timestamp. We also don't assume the fingerprint
         * or hostname will change.
         */
        if (memcmp(&dl->dl_ipaddr, &st->ds_leases[i].dl_ipaddr, sizeof(dl->dl_ipaddr)))
        {
            continue;
        }
        if (memcmp(&dl->dl_hwaddr, &st->ds_leases[i].dl_hwaddr, sizeof(dl->dl_hwaddr)))
        {
            continue;
        }

        return true;
    }

    return false;
}

/*
 * Add a single lease entry to the cache
 */
static void dhcp_server_lease_add(osn_dhcp_server_t *self, struct osn_dhcp_server_lease *dl)
{
    struct osn_dhcp_server_status *st = &self->ds_status;

    if (lease_exists(st, dl))
    {
           LOGT("Lease is already added, skipping.");
           return;
    }

    /* Resize the leases array */
    if ((st->ds_leases_len % DHCP_LEASE_RESIZE_QUANTUM) == 0)
    {
        /* Reallocate buffer */
        st->ds_leases = realloc(
                st->ds_leases,
                (st->ds_leases_len + DHCP_LEASE_RESIZE_QUANTUM) * sizeof(struct osn_dhcp_server_lease));
    }

    LOG(DEBUG, "New lease added: "PRI_osn_ip_addr, FMT_osn_ip_addr(dl->dl_ipaddr));

    /* Append to array */
    st->ds_leases[st->ds_leases_len++] = *dl;
}

static osn_dhcp_server_t* dhcp_server_find_by_lease(struct osn_dhcp_server_lease *dl)
{
    osn_dhcp_server_t *ds;

    /* Traverse list of server instances*/
    ds_dlist_foreach(&dhcp_server_list, ds)
    {
        /* As we don't have a control over DHPC server
        * return first found instance.
        */
        return ds;
    }

    LOG(WARNING, "No dhcp server instance found");

    return NULL;
}

/*
 * Parse the dnsmasq lease file and update the lease cache
 */
bool dhcp_lease_parse(void)
{
    osn_dhcp_server_t *ds;
    FILE *lf;
    char line[1024];

    bool retval = false;

    lf = fopen(CONFIG_RDK_DHCP_LEASES_PATH, "r");
    if (lf == NULL)
    {
        LOGE("dhcpv4_server: Error opening lease file: %s", CONFIG_RDK_DHCP_LEASES_PATH);
        goto exit;
    }

    /*
     * Lock the file -- dnsmasq was patched to to the same -- this ensures we're
     * not reading a half-written file.
     *
     * When the file descriptor is closed, the associated lock should be released
     * as well.
     */
    if (!os_file_lock(fileno(lf), OS_LOCK_READ))
    {
        LOGE("dhcpv4_server: Error locking lease file: %s", CONFIG_RDK_DHCP_LEASES_PATH);
        goto exit;
    }

    while (fgets(line, sizeof(line), lf) != NULL)
    {
        struct osn_dhcp_server_lease dl;

        if (!dhcp_server_lease_parse_line(&dl, line))
        {
            LOGW("dhcpv4_server: Error parsing DHCP lease line: %s", line);
            continue;
        }

        /* Find the server instance that this lease belongs to */
        ds = dhcp_server_find_by_lease(&dl);
        if (ds == NULL)
        {
            LOGN("dhcpv4_server: Unable find server instance associated with lease: "PRI_osn_ip_addr,
                    FMT_osn_ip_addr(dl.dl_ipaddr));
            continue;
        }

        dhcp_server_lease_add(ds, &dl);
    }

    retval = true;
exit:
    if (lf != NULL) fclose(lf);

    return retval;
}

/*
 * Callback function triggered by file status change on the lease file
 */
void dhcp_lease_onchange(struct ev_loop *loop, ev_stat *w, int revent)
{
    (void)loop;
    (void)revent;

    osn_dhcp_server_t *ds;

    /* Clear all leases */
    ds_dlist_foreach(&dhcp_server_list, ds)
    {
        dhcp_lease_clear(ds);
    }

    if (w->attr.st_nlink)
    {
        LOGI("dhcpv4_server: Lease file changed.");
        dhcp_lease_parse();
    }
    else
    {
        LOGI("dhcpv4_server: Lease file removed, flushing all entries.");
    }

    /* Send out status change notifications */
    dhcp_server_status_dispatch();
}


void dhcp_lease_init(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    (void)loop;
    (void)revent;

    osn_dhcp_server_t *ds;

    LOG(INFO, "DHCP leases initialization");

    /* Clear all leases */
    ds_dlist_foreach(&dhcp_server_list, ds)
    {
        dhcp_lease_clear(ds);
    }

    dhcp_lease_parse();

    /* Send out status change notifications */
    dhcp_server_status_dispatch();

}
