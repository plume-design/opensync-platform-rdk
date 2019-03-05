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
 * wifihal_radio.c
 *
 * RDKB Platform - Wifi HAL - Radio APIs
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

#define MODULE_ID   LOG_MODULE_ID_HAL

#define IWPRIV_DOTH_CHAN_SWITCH         "doth_ch_chwidth"

#define CSA_TBTT                        25  // Increase due to XB devices 100ms beacon

#define STATE_UPDATE_AFTER_CSA          10  // seconds
#define WIFIHAL_RADIO_UPDATE_DELAY      5   // seconds

/*****************************************************************************/

enum
{
    CSA_ARG_CHAN            = 0,
    CSA_ARG_TBTT,
    CSA_ARG_CHAN_BW,
    CSA_ARG_COUNT
};


static c_item_t map_band_str[] = {
    C_ITEM_STR_STR("2.4GHz",                    "2.4G"),
    C_ITEM_STR_STR("5GHz",                      "5G")
};

static c_item_t map_htmode_str[] = {
    C_ITEM_STR_STR("20MHz",                     "HT20"),
    C_ITEM_STR_STR("40MHz",                     "HT40"),
    C_ITEM_STR_STR("80MHz",                     "HT80"),
    C_ITEM_STR_STR("80+80MHz",                  "HT80+80"),
    C_ITEM_STR_STR("80+80",                     "HT80+80"),
    C_ITEM_STR_STR("160MHz",                    "HT160"),
    C_ITEM_STR_STR("160",                       "HT160")
};

static c_item_t map_country_str[] = {
    C_ITEM_STR_STR("840",                       "US"),
    C_ITEM_STR_STR("841",                       "US"),
    C_ITEM_STR_STR("us",                        "US"),   // QTN returns "us"
    C_ITEM_STR_STR("Q2I",                       "US")    // BCM_WIFI returns "Q2I"
};

static c_item_t map_chan_mode[] = {
    C_ITEM_STR(WIFIHAL_CHAN_MODE_MANUAL,        "manual"),
    C_ITEM_STR(WIFIHAL_CHAN_MODE_CLOUD,         "cloud"),
    C_ITEM_STR(WIFIHAL_CHAN_MODE_AUTO,          "auto")
};

static c_item_t map_csa_chanwidth[] = {
    C_ITEM_STR(20,                              "HT20"),
    C_ITEM_STR(40,                              "HT40"),
    C_ITEM_STR(80,                              "HT80"),
    C_ITEM_STR(80,                              "HT80+80"),
    C_ITEM_STR(160,                             "HT160")
};

static wifihal_radio_state_cb_t     *wifihal_radio_state_cb = NULL;
static wifihal_radio_config_cb_t    *wifihal_radio_config_cb = NULL;

/*****************************************************************************/

static void
wifihal_task_radio_state_update(void *arg)
{
    wifihal_radio_t     *radio = arg;
    wifihal_ssid_t      *ssid;

    LOGI("%s: Updating state table", radio->ifname);

    wifihal_radio_state_update(radio);

    ds_dlist_foreach(&radio->ssids, ssid)
    {
        LOGI("%s: Updating state table", ssid->ifname);
        wifihal_vif_state_update(ssid);
    }

    radio->csa_in_progress = false;

    return;
}

#ifdef QTN_WIFI
#define WIFIHAL_RADIO_REAPPLY           2  // seconds

static void
wifihal_task_radio_reapply_config(void *arg)
{
    wifihal_radio_t     *radio = arg;
    INT                 ret;

    WIFIHAL_TM_START();
    ret = wifi_applyRadioSettings(radio->index);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_applyRadioSettings(%d)",
                                  radio->index);

    if (ret == RETURN_OK) {
        LOGI("%s: Re-applied radio config", radio->ifname);
    } else {
        LOGW("%s: Re-applying radio config failed", radio->ifname);
    }

    return;
}
#endif /* QTN_WIFI */

static bool
wifihal_radio_change_channel(wifihal_radio_t *radio, int channel, char *ht_mode)
{
    c_item_t            *citem;
    INT                 ret;
    int                 ch_width = 0;

    if (!ht_mode) {
        LOGE("%s: No ht_mode configured, cannot change channel!", radio->ifname);
        return false;
    }

    if (!(citem = c_get_item_by_str(map_csa_chanwidth, ht_mode)))
    {
        LOGE("%s: Failed to change channel -- HT Mode '%s' unsupported",
             radio->ifname, ht_mode);
        return false;
    }
    ch_width = citem->key;

    WIFIHAL_TM_START();
    ret = wifi_pushRadioChannel2(radio->index, channel, ch_width, CSA_TBTT);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_pushRadioChannel2(%d, %d, %d, %d)",
                                 radio->index, channel, ch_width, CSA_TBTT);
    LOGD("[WIFI_HAL SET] wifi_pushRadioChannel2(%d, %d, %d, %d) = %d",
         radio->index, channel, ch_width, CSA_TBTT, ret);
    if (ret != RETURN_OK) {
        LOGE("%s: Failed to push new channel %d", radio->ifname, channel);
        return false;
    }

    radio->csa_in_progress = true;
    LOGW("%s: Started CSA to channel %d, width %d, tbtt %d",
         radio->ifname, channel, ch_width, CSA_TBTT);

#ifdef QTN_WIFI
    // Workaround for intermittent QTN bug
    evsched_task(&wifihal_task_radio_reapply_config, radio, EVSCHED_SEC(WIFIHAL_RADIO_REAPPLY));
#endif

    return true;
}

static void
wifihal_radio_update_task(void *arg)
{
    struct schema_Wifi_Radio_Config rconf;
    wifihal_radio_t                 *radio = arg;

    if (!wifihal_radio_config_cb) {
        LOGW("%s: update task run without any config callback!", radio->ifname);
        return;
    }

    if (!wifihal_radio_config_get(radio->ifname, &rconf))
    {
        LOGE("%s: update task failed -- unable to retrieve config", radio->ifname);
        return;
    }

    LOGN("%s: Updating configuration...", radio->ifname);
    wifihal_radio_config_cb(&rconf, NULL);

    wifihal_radio_state_update(radio);

    return;
}

/*****************************************************************************/

bool
wifihal_radio_state_register(char *ifname, wifihal_radio_state_cb_t *radio_state_cb)
{
    (void)ifname;
    wifihal_radio_state_cb = radio_state_cb;

    return true;
}

bool
wifihal_radio_config_register(const char *ifname, void *rconf_cb)
{
    (void)ifname;
    wifihal_radio_config_cb = rconf_cb;

    return true;
}

bool
wifihal_radio_config_get(const char *ifname, struct schema_Wifi_Radio_Config *rconf)
{
    wifihal_radio_t     *radio;
    ULONG               lval;
    CHAR                buf[WIFIHAL_MAX_BUFFER];
    BOOL                bval;
    char                *str;
    INT                 ret;

    if (!(radio = wifihal_radio_by_ifname(ifname)))
    {
        LOGW("%s: Get config failed -- radio not found", ifname);
        return false;
    }
    memset(rconf, 0, sizeof(*rconf));

    // if_name (w/ exists)
    strncpy(rconf->if_name,
#ifdef BCM_WIFI  // Uses same radio name as home-ap, so mapping breaks
            radio->ifname,
#else
            target_unmap_ifname(radio->ifname),
#endif
            sizeof(rconf->if_name)-1);
    rconf->if_name_exists = true;
    LOGT("%s: ifname %s %s", __FUNCTION__, radio->ifname, rconf->if_name);

    // freq_band
    str = c_get_str_by_strkey(map_band_str, radio->band);
    if (strlen(str) == 0) {
        LOGW("%s: Failed to decode band string (%s)", radio->ifname, radio->band);
    }
    strncpy(rconf->freq_band, str, sizeof(rconf->freq_band)-1);

    // hw_type (w/ exists)
    str = target_radio_get_chipset(radio->ifname);
    if (strlen(str) == 0) {
        LOGW("%s: Failed to get wifi chipset type", radio->ifname);
    }
    strncpy(rconf->hw_type, str, sizeof(rconf->hw_type)-1);
    rconf->hw_type_exists = true;

    // enabled (w/ exists)
    rconf->enabled = radio->enabled;
    rconf->enabled_exists = true;

    // channel_sync (w/ exists)
    rconf->channel_sync = radio->channel_sync;
    rconf->channel_sync_exists = true;

    // channel (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioChannel(radio->index, &lval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioChannel(%d) ret %ld",
                               radio->index, lval);
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get channel number", radio->ifname);
    }
    else
    {
        rconf->channel = lval;
        rconf->channel_exists = true;

        // Cache channel for health checking
        radio->channel = rconf->channel;
    }

    // channel_mode (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioAutoChannelEnable(radio->index, &bval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioAutoChannelEnable(%d) ret %s",
                                         radio->index, bval ? "true" : "false");
    if (ret != RETURN_OK)
    {
        LOGW("%s: Radio #%d unable to detect auto channel enable", radio->ifname, radio->index);
    }
    else
    {
        radio->auto_chan = bval;
    }
    str = c_get_str_by_key(map_chan_mode, wifihal_cloud_mode_get_chan_mode(radio));
    if (str && strlen(str) > 0)
    {
        strncpy(rconf->channel_mode, str, sizeof(rconf->channel_mode)-1);
        rconf->channel_mode_exists = true;
    }

    // tx_power (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioTransmitPower(radio->index, &lval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioTransmitPower(%d) ret %ld",
                                     radio->index, lval);
    if (ret == RETURN_OK)
    {
        rconf->tx_power = lval;
        if (rconf->tx_power > 1 && rconf->tx_power < 33)
        {
            rconf->tx_power_exists = true;
        }
    }

    // country (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioCountryCode(radio->index, buf);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioCountryCode(%d) ret \"%s\"",
                                   radio->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get country code", radio->ifname);
    }
    else
    {
        str = c_get_str_by_strkey(map_country_str, buf);
        if (strlen(str) == 0)
        {
            LOGW("%s: Failed to decode country (%s)", radio->ifname, buf);
            str = buf;
        }
        strncpy(rconf->country, str, sizeof(rconf->country)-1);
        rconf->country_exists = true;
    }

    // ht_mode (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioOperatingChannelBandwidth(radio->index, buf);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioOperatingChannelBandwidth(%d) ret \"%s\"",
                                                 radio->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get ht_mode", radio->ifname);
    }
    else
    {
        str = c_get_str_by_strkey(map_htmode_str, buf);
        if (strlen(str) == 0)
        {
            LOGW("%s: Failed to decode ht_mode (%s)", radio->ifname, buf);
        }
        strncpy(rconf->ht_mode, str, sizeof(rconf->ht_mode)-1);
        rconf->ht_mode_exists = true;
    }

    // hw_mode (w/ exists)
    WIFIHAL_TM_START();
    ret = wifi_getRadioStandard(radio->index, buf, &bval, &bval, &bval);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioStandard(%d) ret \"%s\"",
                                radio->index, (ret == RETURN_OK) ? buf : "");
    if (ret != RETURN_OK)
    {
        LOGW("%s: Failed to get hw_mode", radio->ifname);
    }
    else
    {
        snprintf(rconf->hw_mode, sizeof(rconf->hw_mode)-1, "11%s", buf);
        rconf->hw_mode_exists = true;
    }

    return true;
}

bool
wifihal_radio_state_get(const char *ifname, struct schema_Wifi_Radio_State *rstate,
        schema_filter_t *filter)
{
    struct schema_Wifi_Radio_Config     rconf;
    wifihal_radio_t                     *radio;
    os_macaddr_t                        macaddr;
    CHAR                                pchannels[64];
    char                                *p;
    int                                 chan;
    int                                 ret;
    int                                 i;

    if (!(radio = wifihal_radio_by_ifname(ifname)))
    {
        LOGW("%s: Get state failed -- radio not found", ifname);
        return false;
    }

    // NOTE: Most is copied from config
    if (!wifihal_radio_config_get(ifname, &rconf))
    {
        LOGW("%s: Get state failed to get config", ifname);
        return false;
    }
    memset(rstate, 0, sizeof(*rstate));
    memset(filter, 0, sizeof(*filter));
    schema_filter_init(filter,  "+");

#define RADIO_STATE_COPY_OPT(TYPE, FIELD) \
    if (rconf.FIELD##_exists) { \
        SCHEMA_FF_SET_##TYPE(filter, rstate, FIELD, rconf.FIELD); \
    }

#define RADIO_STATE_COPY_REQ(TYPE, FIELD) \
    SCHEMA_FF_SET_##TYPE##_REQ(filter, rstate, FIELD, rconf.FIELD); \

    // Copy from config
    RADIO_STATE_COPY_OPT(STR, if_name);
    RADIO_STATE_COPY_REQ(STR, freq_band);
    RADIO_STATE_COPY_OPT(STR, hw_type);
    RADIO_STATE_COPY_OPT(INT, enabled);
    RADIO_STATE_COPY_OPT(INT, channel);
    RADIO_STATE_COPY_OPT(STR, channel_mode);
    RADIO_STATE_COPY_OPT(INT, tx_power);
    RADIO_STATE_COPY_OPT(STR, country);
    RADIO_STATE_COPY_OPT(STR, ht_mode);
    RADIO_STATE_COPY_OPT(STR, hw_mode);

    // Additional items besides config
    if (os_nif_macaddr(radio->ifname, &macaddr))
    {
        dpp_mac_to_str(macaddr.addr, rstate->mac);
        rstate->mac_exists = true;
        schema_filter_add(filter, SCHEMA_COLUMN(Wifi_Radio_State, mac));
    }

    // Possible Channels
    WIFIHAL_TM_START();
    ret = wifi_getRadioPossibleChannels(radio->index, pchannels);
    WIFIHAL_TM_STOP(ret, WIFIHAL_STD_TIME, "wifi_getRadioPossibleChannels(%d) ret [%s]",
                                        radio->index, pchannels);
    if (ret == RETURN_OK)
    {
        strcat(pchannels, ",");
        i = 0;
        p = strtok(pchannels, ",");
        while (p)
        {
            chan = atoi(p);
            if (chan >= 1 && chan <= 165) {
                rstate->allowed_channels[i++] = chan;
            }

            p = strtok(NULL, ",");
        }
        rstate->allowed_channels_len = i;
    }
    else
    {
        LOGW("%s: Failed to get possible channels", radio->ifname);
    }

    return true;
}

bool
wifihal_radio_state_update(wifihal_radio_t *radio)
{
    struct schema_Wifi_Radio_State  rstate;
    schema_filter_t filter;

    if (!wifihal_radio_state_cb) {
        LOGW("%s: Radio state update skipped -- no callback registered", radio->ifname);
        return false;
    }

    if (!wifihal_radio_state_get(radio->ifname, &rstate, &filter))
    {
        LOGE("%s: Radio state update failed -- unable to get state", radio->ifname);
        return false;
    }

    LOGN("%s: Updating state...", radio->ifname);
    wifihal_radio_state_cb(&rstate, &filter);

    return true;
}

bool
wifihal_radio_update(const char *ifname, struct schema_Wifi_Radio_Config *rconf)
{
    struct schema_Wifi_Radio_Config     cur_rconf;
    wifihal_radio_t                     *radio;
    char                                *ht_mode = NULL;
    int                                 state_update_tm = -1;
    bool                                force_ch_change = false;

    if (!(radio = wifihal_radio_by_ifname(ifname)))
    {
        LOGW("%s: Update RADIO failed -- radio not found", ifname);
        return false;
    }

    if (!wifihal_radio_config_get(ifname, &cur_rconf))
    {
        LOGW("%s: Update RADIO failed -- unable to get current config", ifname);
        return false;
    }

#define UPDATE_STATE(x) state_update_tm = (x > state_update_tm) ? x : state_update_tm
#define BOTH_HAS(x)     (rconf->x##_exists && cur_rconf.x##_exists)
#define CHANGED(x)      (BOTH_HAS(x) && rconf->x != cur_rconf.x)
#define STR_CHANGED(x)  (BOTH_HAS(x) && strcmp(rconf->x, cur_rconf.x))

    if (CHANGED(channel_sync))
    {
        radio->channel_sync = rconf->channel_sync;
        force_ch_change = true;
    }

    if (CHANGED(enabled))
    {
        radio->enabled = rconf->enabled;
        force_ch_change = true;
    }
    if (CHANGED(channel) || force_ch_change)
    {
        if (rconf->ht_mode_exists) {
            ht_mode = rconf->ht_mode;
        } else if (cur_rconf.ht_mode_exists) {
            ht_mode = cur_rconf.ht_mode;
        }

        if (CHANGED(channel)) {
            LOGN("%s: Changing channel from %d to %d using CSA...",
                 ifname, cur_rconf.channel, rconf->channel);
        } else {
            LOGN("%s: OVSDB forced channel change to %d",
                 ifname, rconf->channel);
        }

        if (wifihal_radio_change_channel(radio, rconf->channel, ht_mode))
        {
            UPDATE_STATE(STATE_UPDATE_AFTER_CSA);  // schedule state update
        }
    }

#undef UPDATE_STATE
#undef BOTH_HAS
#undef CHANGED
#undef STR_CHANGED

    if (state_update_tm >= 0) {
        evsched_task(&wifihal_task_radio_state_update, radio, EVSCHED_SEC(state_update_tm));
    }

    return true;
}

bool
wifihal_radio_updated(wifihal_radio_t *radio)
{
    // Cancel any previously scheduled update task
    evsched_task_cancel_by_find(
            wifihal_radio_update_task,
            radio,
            (EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG));

    // Schedule update task
    evsched_task(
            wifihal_radio_update_task,
            radio,
            EVSCHED_SEC(WIFIHAL_RADIO_UPDATE_DELAY));

    return true;
}

bool
wifihal_radio_state_updated(wifihal_radio_t *radio)
{
    // Cancel any previously scheduled update task
    evsched_task_cancel_by_find(
            wifihal_task_radio_state_update,
            radio,
            (EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG));

    // Schedule update task
    evsched_task(
            wifihal_task_radio_state_update,
            radio,
            EVSCHED_SEC(WIFIHAL_RADIO_UPDATE_DELAY));

    return true;
}

bool
wifihal_radio_all_updated(void)
{
    wifihal_radio_t     *radio;
    ds_dlist_t          *radios = wifihal_get_radios();

    ds_dlist_foreach(radios, radio) {
        wifihal_radio_updated(radio);
    }

    return true;
}
