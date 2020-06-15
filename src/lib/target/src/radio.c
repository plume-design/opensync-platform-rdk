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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <ev.h>

#include "log.h"
#include "const.h"
#include "target.h"
#include "target_internal.h"
#include "os_nif.h"
#include "evsched.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

#define MODULE_ID LOG_MODULE_ID_RADIO

#define CSA_TBTT                        25
#define RESYNC_UPDATE_DELAY_SECONDS     5

/*****************************************************************************/

static c_item_t map_band_str[] =
{
    C_ITEM_STR_STR("2.4GHz",                    "2.4G"),
    C_ITEM_STR_STR("5GHz",                      "5G")
};

static c_item_t map_htmode_str[] =
{
    C_ITEM_STR_STR("20MHz",                     "HT20"),
    C_ITEM_STR_STR("40MHz",                     "HT40"),
    C_ITEM_STR_STR("80MHz",                     "HT80"),
    C_ITEM_STR_STR("80+80MHz",                  "HT80+80"),
    C_ITEM_STR_STR("80+80",                     "HT80+80"),
    C_ITEM_STR_STR("160MHz",                    "HT160"),
    C_ITEM_STR_STR("160",                       "HT160")
};

typedef enum
{
    TARGET_RADIO_CHAN_MODE_MANUAL    = 0,
    TARGET_RADIO_CHAN_MODE_AUTO      = 1,
    TARGET_RADIO_CHAN_MODE_CLOUD     = 2,
} target_radio_chan_mode_t;

static c_item_t map_csa_chanwidth[] =
{
    C_ITEM_STR(20,                              "HT20"),
    C_ITEM_STR(40,                              "HT40"),
    C_ITEM_STR(80,                              "HT80"),
    C_ITEM_STR(80,                              "HT80+80"),
    C_ITEM_STR(160,                             "HT160")
};

static radio_cloud_mode_t radio_cloud_mode = RADIO_CLOUD_MODE_UNKNOWN;

static bool dfs_event_cb_registered = false;
static char dfs_last_channel[32];             // last channel reported by driver in radar event
static char dfs_num_detected[32];             // number of DFS events detected
static unsigned int dfs_radar_timestamp = 0;  // saved timestamp from radar detection event

static struct target_radio_ops g_rops;
static bool g_resync_ongoing = false;

bool target_radio_config_need_reset()
{
    return true;
}

static void healthcheck_task(void *arg)
{
    LOGI("Healthcheck re-sync");
    radio_trigger_resync();
    evsched_task_reschedule_ms(EVSCHED_SEC(CONFIG_RDK_HEALTHCHECK_INTERVAL));
}

bool target_radio_init(const struct target_radio_ops *ops)
{
    /* Register callbacks */
    g_rops = *ops;

    if (!clients_hal_init(ops))
    {
        LOGW("Cannot initialize clients");
    }

    evsched_task(&healthcheck_task, NULL, EVSCHED_SEC(2));
    return true;
}

static bool radio_change_channel(
        INT radioIndex,
        int channel,
        const char *ht_mode)
{
    c_item_t            *citem;
    INT                 ret;
    int                 ch_width = 0;
    char                radio_ifname[128];

    LOGD("Switch to channel %d triggered", channel);

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radioIndex, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio ifname for idx %d", __func__,
                radioIndex);
        return false;
    }

    if (ht_mode == NULL)
    {
        LOGE("%s: No ht_mode configured, cannot change channel!", radio_ifname);
        return false;
    }

    if ((citem = c_get_item_by_str(map_csa_chanwidth, ht_mode)) == NULL)
    {
        LOGE("%s: Failed to change channel -- HT Mode '%s' unsupported",
             radio_ifname, ht_mode);
        return false;
    }
    ch_width = citem->key;

    ret = wifi_pushRadioChannel2(radioIndex, channel, ch_width, CSA_TBTT);
    LOGD("[WIFI_HAL SET] wifi_pushRadioChannel2(%d, %d, %d, %d) = %d",
         radioIndex, channel, ch_width, CSA_TBTT, ret);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to push new channel %d", radio_ifname, channel);
        return false;
    }

    if (!sync_send_channel_change(radioIndex, channel))
    {
        LOGW("%d: Failed to sync channel change to %u", radioIndex, channel);
    }

    if (!sync_send_channel_bw_change(radioIndex, ch_width))
    {
        LOGW("%d: Failed to sync channel bandwidth change to %u MHz", radioIndex, ch_width);
    }

    LOGI("%s: Started CSA to channel %d, width %d, tbtt %d",
         radio_ifname, channel, ch_width, CSA_TBTT);

    return true;
}

static const char* channel_state_to_state_str(wifi_channelState_t state)
{
    switch (state)
    {
        case CHAN_STATE_AVAILABLE:
            return "{\"state\":\"allowed\"}";
        case CHAN_STATE_DFS_NOP_FINISHED:
            return "{\"state\": \"nop_finished\"}";
        case CHAN_STATE_DFS_NOP_START:
            return "{\"state\": \"nop_started\"}";
        case CHAN_STATE_DFS_CAC_START:
            return "{\"state\": \"cac_started\"}";
        case CHAN_STATE_DFS_CAC_COMPLETED:
            return "{\"state\": \"cac_completed\"}";
        default:
            LOGE("Unknown channel state: %d\n", state);
    }

    return NULL;
}

static bool update_channels_map(
        struct schema_Wifi_Radio_State *rstate,
        INT radioIndex)
{
    int i;
    int j = 0;
    int ret;
    const int MAP_SIZE = 24;
    wifi_channelMap_t channel_map[MAP_SIZE];

    memset(channel_map, 0, sizeof(channel_map));

    ret = wifi_getRadioChannels(radioIndex, channel_map, MAP_SIZE);
    if (ret != RETURN_OK)
    {
        LOGE("Cannot get channel map for %d\n", radioIndex);
        return false;
    }

    for (i = 0; i < MAP_SIZE; i++)
    {
        if (channel_map[i].ch_number == 0)
        {
            // If channel number is not set it means we don't have data for it
            continue;
        }

        LOGT("Adding channel %d state %d to channel map\n", channel_map[i].ch_number,
             channel_map[i].ch_state);
        snprintf(rstate->channels_keys[j], sizeof(rstate->channels_keys[j]),
                 "%d", channel_map[i].ch_number);
        STRSCPY_WARN(rstate->channels[j], channel_state_to_state_str(channel_map[i].ch_state));
        j++;
    }

    rstate->channels_len = j;

    return true;
}

static void update_radar_info(struct schema_Wifi_Radio_State *rstate)
{
    STRSCPY_WARN(rstate->radar_keys[0], "last_channel");
    STRSCPY_WARN(rstate->radar[0], strlen(dfs_last_channel) > 0 ? dfs_last_channel : "0");

    STRSCPY_WARN(rstate->radar_keys[1], "num_detected");

    STRSCPY_WARN(rstate->radar_keys[2], "time");
    snprintf(rstate->radar[2], sizeof(rstate->radar[2]), "%u", dfs_radar_timestamp);

    rstate->radar_len = 3;
}

static bool radio_state_get(
        INT radioIndex,
        struct schema_Wifi_Radio_State *rstate)
{
    os_macaddr_t                        macaddr;
    CHAR                                pchannels[64];
    char                                *p;
    int                                 chan;
    int                                 ret;
    int                                 i;
    char                                radio_ifname[128];
    char                                band[128];
    char                                *str;
    BOOL                                enabled;
    ULONG                               lval;
    BOOL                                bval;
    CHAR                                buf[WIFIHAL_MAX_BUFFER];


    memset(rstate, 0, sizeof(*rstate));
    schema_Wifi_Radio_State_mark_all_present(rstate);
    rstate->_partial_update = true;
    rstate->channel_sync_present = false;
    rstate->channel_mode_present = false;
    rstate->radio_config_present = false;
    rstate->vif_states_present = false;

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radioIndex, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio ifname for idx %d", __func__, radioIndex);
        return false;
    }

    // TODO: use SCHEMA_SET_STR etc. (?)
    // if_name (w/ exists)
    STRSCPY(rstate->if_name, target_unmap_ifname((char *)radio_ifname));
    rstate->if_name_exists = true;

    memset(band, 0, sizeof(band));
    ret = wifi_getRadioOperatingFrequencyBand(radioIndex, band);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio band for %s\n", __func__, radio_ifname);
        return false;
    }
    // freq_band
    str = c_get_str_by_strkey(map_band_str, band);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to decode band string (%s)", radio_ifname, band);
    }
    STRSCPY(rstate->freq_band, str);

    // hw_type (w/ exists)
    str = target_radio_get_chipset(radio_ifname);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to get wifi chipset type", radio_ifname);
    }
    STRSCPY(rstate->hw_type, str);
    rstate->hw_type_exists = true;

    ret = wifi_getRadioEnable(radioIndex, &enabled);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get enabled state for radio %s", __func__,
                radio_ifname);
        return false;
    }
    // enabled (w/ exists)
    rstate->enabled = enabled;
    rstate->enabled_exists = true;


    // channel (w/ exists)
    ret = wifi_getRadioChannel(radioIndex, &lval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get channel number", radio_ifname);
    }
    else
    {
        rstate->channel = lval;
        rstate->channel_exists = true;
    }

    // tx_power (w/ exists)
    ret = wifi_getRadioTransmitPower(radioIndex, &lval);
    if (ret == RETURN_OK)
    {
        // WAR: in schema the max txpower is between 1 and 32
        if (lval > 32) {
            lval = 32;
        } else if (lval < 1) {
            lval = 1;
        }
        rstate->tx_power = lval;
        rstate->tx_power_exists = true;
    } else
    {
        LOGW("Cannot read tx power for radio index %d", radioIndex);
    }

    // country (w/ exists)
    memset(buf, 0, sizeof(buf));
    if (osync_hal_get_country_code(radio_ifname, buf, sizeof(buf)) != OSYNC_HAL_SUCCESS)
    {
        LOGW("%s: Failed to get country code", radio_ifname);
    }
    else
    {
        STRSCPY(rstate->country, buf);
        rstate->country_exists = true;
    }

    // ht_mode (w/ exists)
    memset(buf, 0, sizeof(buf));
    ret = wifi_getRadioOperatingChannelBandwidth(radioIndex, buf);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get ht_mode", radio_ifname);
    }
    else
    {
        str = c_get_str_by_strkey(map_htmode_str, buf);
        if (strlen(str) == 0)
        {
            LOGW("%s: Failed to decode ht_mode (%s)", radio_ifname, buf);
        }
        STRSCPY(rstate->ht_mode, str);
        rstate->ht_mode_exists = true;
    }

    // hw_mode (w/ exists)
    memset(buf, 0, sizeof(buf));
    ret = wifi_getRadioStandard(radioIndex, buf, &bval, &bval, &bval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get hw_mode", radio_ifname);
    }
    else
    {
        snprintf(rstate->hw_mode, sizeof(rstate->hw_mode) - 1, "11%s", buf);
        rstate->hw_mode_exists = true;
    }

    // Update DFS data
    if (!update_channels_map(rstate, radioIndex))
    {
        LOGW("Cannot update channels map");
    }

    update_radar_info(rstate);

    if (os_nif_macaddr((char *)radio_ifname, &macaddr))
    {
        dpp_mac_to_str(macaddr.addr, rstate->mac);
        rstate->mac_exists = true;
    }

    // Possible Channels
    ret = wifi_getRadioPossibleChannels(radioIndex, pchannels);
    if (ret == RETURN_OK)
    {
        strcat(pchannels, ",");
        i = 0;
        p = strtok(pchannels, ",");
        while (p)
        {
            chan = atoi(p);
            if (chan >= 1 && chan <= 165)
            {
                rstate->allowed_channels[i++] = chan;
            }

            p = strtok(NULL, ",");
        }
        rstate->allowed_channels_len = i;
    }
    else
    {
        LOGW("%s: Failed to get possible channels", radio_ifname);
    }

    LOGN("Get radio state completed for radio index %d", radioIndex);
    return true;
}

static bool radio_state_update(UINT radioIndex)
{
    struct schema_Wifi_Radio_State  rstate;

    if (!radio_state_get(radioIndex, &rstate))
    {
        LOGE("%s: Radio state update failed -- unable to get state for idx %d",
             __func__, radioIndex);
        return false;
    }
    LOGN("Updating state for radio index %d...", radioIndex);
    g_rops.op_rstate(&rstate);

    return true;
}

static void chan_event_cb(
        UINT radioIndex,
        wifi_chan_eventType_t event,
        UCHAR channel)
{
    struct timeval tv;

    switch (event)
    {
        case WIFI_EVENT_CHANNELS_CHANGED:
            LOGD("CHANNELS CHANGED, last_channel = %d\n", channel);
            radio_state_update(radioIndex);
            return;
        case WIFI_EVENT_DFS_RADAR_DETECTED:
            LOGD("DFS RADAR DETECTED, last_channel = %d\n", channel);

            // Save last channel
            snprintf(dfs_last_channel, sizeof(dfs_last_channel), "%d", channel);

            // dfs_num_detected is always set to "1" currently
            STRSCPY_WARN(dfs_num_detected, "1");

            // Save timestamp
            if (!gettimeofday(&tv, NULL))
            {
                dfs_radar_timestamp = (unsigned int)tv.tv_sec;
            } else
            {
                dfs_radar_timestamp = 0;
                LOGW("Cannot get timestamp for radar event: %s\n", strerror(errno));
            }

            radio_state_update(radioIndex);
            return;
        default:
            LOGE("Unknown channel event: %d\n", event);
            return;
    }
}

static bool radio_copy_config_from_state(
        INT radioIndex,
        struct schema_Wifi_Radio_State *rstate,
        struct schema_Wifi_Radio_Config *rconf)
{
    memset(rconf, 0, sizeof(*rconf));
    schema_Wifi_Radio_Config_mark_all_present(rconf);
    rconf->_partial_update = true;
    rconf->vif_configs_present = false;

    SCHEMA_SET_STR(rconf->if_name, rstate->if_name);
    LOGT("rconf->ifname = %s", rconf->if_name);
    SCHEMA_SET_STR(rconf->freq_band, rstate->freq_band);
    LOGT("rconf->freq_band = %s", rconf->freq_band);
    SCHEMA_SET_STR(rconf->hw_type, rstate->hw_type);
    LOGT("rconf->hw_type = %s", rconf->hw_type);
    SCHEMA_SET_INT(rconf->enabled, rstate->enabled);
    LOGT("rconf->enabled = %d", rconf->enabled);
    SCHEMA_SET_INT(rconf->channel, rstate->channel);
    LOGT("rconf->channel = %d", rconf->channel);
    SCHEMA_SET_INT(rconf->tx_power, rstate->tx_power);
    LOGT("rconf->tx_power = %d", rconf->tx_power);
    SCHEMA_SET_STR(rconf->country, rstate->country);
    LOGT("rconf->country = %s", rconf->country);
    SCHEMA_SET_STR(rconf->ht_mode, rstate->ht_mode);
    LOGT("rconf->ht_mode = %s", rconf->ht_mode);
    SCHEMA_SET_STR(rconf->hw_mode, rstate->hw_mode);
    LOGT("rconf->hw_mode = %s", rconf->hw_mode);

    return true;
}

bool target_radio_config_init2()
{
    INT ret;
    ULONG r;
    ULONG rnum;
    ULONG s;
    ULONG snum;
    INT ssid_radio_idx;
    char ssid_ifname[128];

    struct schema_Wifi_VIF_Config   vconfig;
    struct schema_Wifi_VIF_State    vstate;
    struct schema_Wifi_Radio_Config rconfig;
    struct schema_Wifi_Radio_State  rstate;

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        return false;
    }

    for (r = 0; r < rnum; r++)
    {
        radio_state_get(r, &rstate);
        radio_copy_config_from_state(r, &rstate, &rconfig);
        g_rops.op_rconf(&rconfig);
        g_rops.op_rstate(&rstate);

        ret = wifi_getSSIDNumberOfEntries(&snum);
        if (ret != RETURN_OK)
        {
            LOGE("%s: failed to get SSID count", __func__);
            return false;
        }

        if (snum == 0)
        {
            LOGE("%s: no SSIDs detected", __func__);
            continue;
        }

        for (s = 0; s < snum; s++)
        {
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(s, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGW("%s: failed to get AP name for index %lu. Skipping.\n", __func__, s);
                continue;
            }

            // Filter SSID's that we don't have mappings for
            if (!target_unmap_ifname_exists(ssid_ifname))
            {
                continue;
            }

            ret = wifi_getSSIDRadioIndex(s, &ssid_radio_idx);
            if (ret != RETURN_OK)
            {
                LOGW("Cannot get radio index for SSID %lu", s);
                continue;
            }

            if ((ULONG)ssid_radio_idx != r)
            {
                continue;
            }

            LOGI("Found SSID index %lu: %s", s, ssid_ifname);
            if (!vif_state_get(s, &vstate))
            {
                LOGE("%s: cannot get vif state for SSID index %lu", __func__, s);
                continue;
            }
            if (!vif_copy_to_config(s, &vstate, &vconfig))
            {
                LOGE("%s: cannot copy VIF state to config for SSID index %lu", __func__, s);
                continue;
            }
            g_rops.op_vconf(&vconfig, rconfig.if_name);
            g_rops.op_vstate(&vstate, rstate.if_name);
        }

    }

    if (!dfs_event_cb_registered)
    {
        if (wifi_chan_eventRegister(chan_event_cb) != RETURN_OK)
        {
            LOGE("Failed to register chan event callback\n");
        }

        dfs_event_cb_registered = true;
    }

    return true;
}

bool radio_ifname_to_idx(const char *ifname, INT *outRadioIndex)
{
    INT ret;
    ULONG r, rnum;
    INT radio_index = -1;
    char radio_ifname[128];

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        return false;
    }

    for (r = 0; r < rnum; r++)
    {
        memset(radio_ifname, 0, sizeof(radio_ifname));
        ret = wifi_getRadioIfName(r, radio_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: failed to get radio ifname for idx %ld", __func__,
                    r);
            return false;
        }
        if (!strncmp(radio_ifname, ifname, sizeof(radio_ifname)))
        {
                radio_index = r;
                break;
        }
    }

    if (radio_index == -1)
    {
        LOGE("Cannot get radio index for %s", ifname);
        return false;
    }

    *outRadioIndex = radio_index;
    return true;
}

bool target_radio_config_set2(
        const struct schema_Wifi_Radio_Config *rconf,
        const struct schema_Wifi_Radio_Config_flags *changed)
{
    int radioIndex;

    if (!radio_ifname_to_idx(rconf->if_name, &radioIndex))
    {
        LOGE("%s: cannot get radio index for %s", __func__, rconf->if_name);
        return false;
    }

    if (changed->channel || changed->ht_mode)
    {
        if (!radio_change_channel(radioIndex, rconf->channel, rconf->ht_mode))
        {
            LOGE("%s: cannot change radio channel for %s", __func__, rconf->if_name);
            return false;
        }
    }

    return radio_state_update(radioIndex);
}

static void radio_resync_all_task(void *arg)
{
    INT ret;
    ULONG r, rnum;
    ULONG s, snum;
    char ssid_ifname[128];

    LOGT("Re-sync started");

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        goto out;
    }

    for (r = 0; r < rnum; r++)
    {
        if (!radio_state_update(r))
        {
            LOGW("Cannot update radio state for radio index %lu", r);
            continue;
        }
    }

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        goto out;
    }

    if (snum == 0)
    {
        LOGE("%s: no SSIDs detected", __func__);
        goto out;
    }

    for (s = 0; s < snum; s++)
    {
        memset(ssid_ifname, 0, sizeof(ssid_ifname));
        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get AP name for index %lu. Skipping.\n", __func__, s);
            continue;
        }

        // Filter SSID's that we don't have mappings for
        if (!target_unmap_ifname_exists(ssid_ifname))
        {
            continue;
        }

        // Fetch existing clients
        if (!clients_hal_fetch_existing(s))
        {
            LOGW("Fetching existing clients for %s failed", ssid_ifname);
        }

        if (!vif_state_update(s))
        {
            LOGW("Cannot update VIF state for SSID index %lu", s);
            continue;
        }
    }
out:
    LOGT("Re-sync completed");
    g_resync_ongoing = false;
}

void radio_trigger_resync()
{
    if (!g_resync_ongoing)
    {
        g_resync_ongoing = true;
        LOGI("Radio re-sync scheduled");
        evsched_task(&radio_resync_all_task, NULL,
                EVSCHED_SEC(RESYNC_UPDATE_DELAY_SECONDS));
    } else
    {
        LOGT("Radio re-sync already ongoing!");
    }
}

bool radio_rops_vstate(
        struct schema_Wifi_VIF_State *vstate,
        const char *radio_ifname)
{
    if (!g_rops.op_vstate)
    {
        LOGE("%s: op_vstate not set", __func__);
        return false;
    }

    g_rops.op_vstate(vstate, radio_ifname);
    return true;
}

bool radio_rops_vconfig(
        struct schema_Wifi_VIF_Config *vconf,
        const char *radio_ifname)
{
    if (!g_rops.op_vconf)
    {
        LOGE("%s: op_vconf not set", __func__);
        return false;
    }

    g_rops.op_vconf(vconf, radio_ifname);
    return true;
}

radio_cloud_mode_t radio_cloud_mode_get()
{
    return radio_cloud_mode;
}

bool
radio_cloud_mode_set(radio_cloud_mode_t mode)
{
    radio_cloud_mode = mode;

    return sync_send_status(radio_cloud_mode);
}
