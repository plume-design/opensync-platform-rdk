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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>
#include <ev.h>

#include "os.h"
#include "os_regex.h"
#include "log.h"
#include "ds_tree.h"
#include "const.h"

#include "target.h"


#define MODULE_ID LOG_MODULE_ID_OSA


/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/

#define DHCP_LEASE_PATH "/nvram/dnsmasq.leases"

struct dhcp_lease
{
    struct schema_DHCP_leased_IP    dl_leased_ip;
    bool                            dl_valid;
    ds_tree_node_t                  dl_node;
};

/**
 * List of private functions
 */

static bool dhcp_lease_init(target_dhcp_leased_ip_cb_t *lease_fn);
static void dhcp_lease_file_update(struct ev_loop *loop, ev_stat *w, int revents);
static void dhcp_lease_notify_update(struct dhcp_lease *dl);
static void dhcp_lease_invalidate(void);
static void dhcp_lease_flush(void);
static int dhcp_lease_cmp(void *_a, void *_b);

/**
 * List of private variables
 */

// DHCP lease callback
static target_dhcp_leased_ip_cb_t *dhcp_lease_callback = NULL;

// Regular expression to match a line in the "dhcp.lease" file of the following format:
// 1461412276 f4:09:d8:89:54:4f 192.168.0.181 android-c992b284e24fdd69 1,33,3,6,15,28,51,58,59 01:f4:09:d8:89:54:4f
static const char dhcp_lease_regex[] =
        "^[0-9]+ "                              // Match timestamp
        "(([a-fA-F0-9]{2}:?){6}) "              // Match MAC address
        "(([0-9]+\\.?){4}) "                    // Match IP address
        "(\\*|[a-zA-Z0-9_-]+) "                 // Match hostname, can be "*"
        "(\\*|([0-9]+,?)+) "                    // Match fingerprint, can be "*"
        "[^ ]+$";                               // Match CID, can be "*"

// If a line matches dhcp_parse_re, the following indexes define the fields to be extracted -- see above
#define DHCP_PARSE_RE_HWADDR    1
#define DHCP_PARSE_RE_IPADDR    3
#define DHCP_PARSE_RE_HOST      5
#define DHCP_PARSE_RE_FINGER    6
#define DHCP_PARSE_RE_SZ        (DHCP_PARSE_RE_FINGER + 1)

// Compiled regular expression for parsing the leases file
static regex_t dhcp_lease_regcomp;

// List of currently active DHCP leases
static ds_tree_t dhcp_lease_list = DS_TREE_INIT(dhcp_lease_cmp,
                                                struct dhcp_lease,
                                                dl_node);

// libev watcher for DHCP_LEASE_FILE
static ev_stat dhcp_lease_watcher;


/******************************************************************************
 *  PRIVATE implementation
 *****************************************************************************/

/**
 * Initialize the DHCP lease subsystem
 */
bool dhcp_lease_init(target_dhcp_leased_ip_cb_t *lease_fn)
{
    // Compile the regular expressions used for parsing the dhcp.leases file
    if (regcomp(&dhcp_lease_regcomp, dhcp_lease_regex, REG_EXTENDED) != 0)
    {
        LOGE("DHCP: Unable to parse regular expression: %s", dhcp_lease_regex);
        return false;
    }

    dhcp_lease_callback = lease_fn;

    // Only monitor file if it already exists, otherwise depend on sync msgs
    if (access(DHCP_LEASE_PATH, F_OK) == 0)
    {
        LOGN("DHCP: Monitoring \"%s\"", DHCP_LEASE_PATH);

        ev_stat_init(
                &dhcp_lease_watcher,
                dhcp_lease_file_update,
                DHCP_LEASE_PATH,
                0);

        ev_stat_start(EV_DEFAULT, &dhcp_lease_watcher);

        (void)target_dhcp_lease_parse_file(DHCP_LEASE_PATH);
    }

    return true;
}

/**
 * Watcher callback for the DHCP lease file
 */
static void dhcp_lease_file_update(struct ev_loop *loop, ev_stat *w, int revents)
{
    if (w->attr.st_nlink > 0)
    {
        LOGT("DHCP: Leases file updated.");

        usleep(500000);

        // Update entries
        (void)target_dhcp_lease_parse_file(DHCP_LEASE_PATH);
    }
    else
    {
        /*
         * Ignore deletes: The way file is synchronized on some platforms causes us
         * to get a delete and a create event back-to-back. This results in a lot of
         * log output, OVSDB transactions, etc. There is no case where we would need
         * to handle the deleted file case: dnsmasq should always be running. If it
         * truncates and re-creates the file, our update logic will handle this.
         */
        LOGT("DHCP: Leases file deleted (ignoring)");
    }
}

/**
 * Notify upper layers of a DHCP lease entry update
 */
static void dhcp_lease_notify_update(struct dhcp_lease *dl)
{
    // Upper layers differentiate between updates and deletions by checking
    // the lease time. If the lease time == 0, it is a deletion!
    if (!dl->dl_valid)
    {
        dl->dl_leased_ip.lease_time = 0;
    }

    if (dhcp_lease_callback != NULL)
    {
        dhcp_lease_callback(&dl->dl_leased_ip);
    }
}

/**
 *  Invalidate current DHCP lease list
 */
void dhcp_lease_invalidate(void)
{
    struct dhcp_lease *dl;

    ds_tree_foreach(&dhcp_lease_list, dl)
    {
        dl->dl_valid = false;
    }
}

/**
 * Insert/update entry
 */
bool target_dhcp_lease_upsert(struct schema_DHCP_leased_IP *dlip)
{
    struct dhcp_lease *dl;

    dl = ds_tree_find(&dhcp_lease_list, dlip);
    if (dl == NULL)
    {
        // New entry
        dl = calloc(1, sizeof(*dl));
        if (dl == NULL)
        {
            LOGE("DHCP: Error allocating struct dhcp_lease");
            return false;
        }

        memcpy(&dl->dl_leased_ip, dlip, sizeof(dl->dl_leased_ip));
        ds_tree_insert(&dhcp_lease_list, dl, &dl->dl_leased_ip);
    }
    else
    {
        if (memcmp(dlip, &dl->dl_leased_ip, sizeof(*dlip)) == 0)
        {
            dl->dl_valid = true;
            return true;
        }
        memcpy(&dl->dl_leased_ip, dlip, sizeof(dl->dl_leased_ip));
    }

    // XXX: dnsmasq doesn't record the lease time in dhcp.lease.
    // Instead it records the future timestamp when the lease will expire.
    // This is somewhat unusable, so just use an arbitrary value:
    dl->dl_leased_ip.lease_time = 1;
    dl->dl_valid = true;

    // Notify upper layers
    dhcp_lease_notify_update(dl);

    return true;
}

/**
 * Remove entry
 */
bool target_dhcp_lease_remove(struct schema_DHCP_leased_IP *dlip)
{
    struct dhcp_lease *dl;

    dl = ds_tree_find(&dhcp_lease_list, dlip);
    if (dl == NULL)
    {
        return false;
    }

    // Notify upper layers that we're about to remove this entry
    dl->dl_valid = false;
    dhcp_lease_notify_update(dl);

    // Remove and free the entry
    ds_tree_remove(&dhcp_lease_list, dl);
    memset(dl, 0, sizeof(*dl));
    free(dl);

    return true;
}

/**
 * Parse the the DHCP lease list from the dhcp.leases file and update the current list
 */
bool target_dhcp_lease_parse_file(char *filename)
{
    char line[1024];
    regmatch_t rm[DHCP_PARSE_RE_SZ];
    struct schema_DHCP_leased_IP dlip;
    int cnt = 0;

    bool retval = false;
    FILE *f = NULL;

    f = fopen(filename, "rt");
    if (f == NULL)
    {
        LOGE("DHCP: Failed to open %s", filename);
        goto error;
    }

    // Invalidate all current leases
    dhcp_lease_invalidate();

    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (regexec(&dhcp_lease_regcomp, line, ARRAY_LEN(rm), rm, 0) != 0)
        {
            LOGE("DHCP: Error parsing DHCP lease line: %s", line);
            continue;
        }
        cnt++;

        // Extract the data
        memset(&dlip, 0, sizeof(dlip));

        // XXX: dnsmasq doesn't record the lease time in dhcp.lease.
        // Instead it records the future timestamp when the lease will expire.
        // This is somewhat unusable, so just use an arbitrary value:
        dlip.lease_time = 1;
        os_reg_match_cpy(dlip.hwaddr, sizeof(dlip.hwaddr), line, rm[DHCP_PARSE_RE_HWADDR]);
        os_reg_match_cpy(dlip.inet_addr, sizeof(dlip.inet_addr), line, rm[DHCP_PARSE_RE_IPADDR]);
        os_reg_match_cpy(dlip.hostname, sizeof(dlip.hostname), line, rm[DHCP_PARSE_RE_HOST]);
        os_reg_match_cpy(dlip.fingerprint, sizeof(dlip.fingerprint), line, rm[DHCP_PARSE_RE_FINGER]);

        (void)target_dhcp_lease_upsert(&dlip);
    }

    if (cnt > 0) retval = true;

error:
    if (f != NULL) fclose(f);

    if (retval)
    {
        // Flush stale entries
        dhcp_lease_flush();
    }

    return retval;
}

/**
 * Remove all invalidate entries from the DHCP lease list
 */
void dhcp_lease_flush(void)
{
    ds_tree_iter_t iter;
    struct dhcp_lease *dl;

    for (   dl = ds_tree_ifirst(&iter, &dhcp_lease_list);
            dl != NULL;
            dl = ds_tree_inext(&iter))
    {
        if (dl->dl_valid) continue;

        // Notify upper layers that we're about to remove this entry
        dhcp_lease_notify_update(dl);

        // Remove and free the entry
        ds_tree_iremove(&iter);
        memset(dl, 0, sizeof(*dl));
        free(dl);
    }
}

/**
 * Comparator for "struct schema_DHCP_leased_IP"
 */
int dhcp_lease_cmp(void *_a, void *_b)
{
    int rc;

    struct schema_DHCP_leased_IP *a = _a;
    struct schema_DHCP_leased_IP *b = _b;

    rc = strcmp(a->inet_addr, b->inet_addr);
    if (rc != 0) return rc;

    rc = strcmp(a->hwaddr, b->hwaddr);
    if (rc != 0) return rc;

    return 0;
}


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

bool
target_dhcp_leased_ip_register(target_dhcp_leased_ip_cb_t *dlip_cb)
{
    static bool dhcp_initialized = false;

    if (!dhcp_initialized)
    {
        if (!dhcp_lease_init(dlip_cb))
        {
            LOGE("Error initializing DHCP lease subsystem.");
            return false;
        }

        dhcp_initialized = true;
    }

    return true;
}
