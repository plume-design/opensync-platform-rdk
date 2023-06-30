/*
Copyright (c) 2021, Plume Design Inc. All rights reserved.

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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "util.h"

#include <ccsp/wifi_hal.h>
#include "memutil.h"

#define LOG(...) printf("WIFIHALTOOL: "  __VA_ARGS__)
#define MAX_NAME_LEN 128
#define MAX_PARAMS_LEN 256
#define MAX_MULTI_PSK_KEYS 30

typedef void (*cmd_handler_t) (int number_of_params, char **params);

typedef struct
{
    char name[MAX_NAME_LEN];
    char params[MAX_PARAMS_LEN];
    cmd_handler_t handler;
    bool variable_number_of_params;
} command_t;

/*
 * To add a new command:
 * 1. Prepare handle_* function which knows how to parse its specific parameters.
 *    The parameters are always passed as "int number_of_params, char **params",
 *    where params is an array of strings containing input parameters.
 *    The "number_of_params" is a number of elements in the "params" array,
 *    and doesn't need to be validated inside the handle_* function
 *    (it is done automatically before the handler is called).
 * 2. Put a forward declaration of this function above the "commands_map" global
 *    variable.
 * 3. Add an entry in "commands_map" global variable. The first parameter is the
 *    name of the function, the second one is a space-delimited list of input
 *    parameters' names for the command. The last parameter is the pointer to
 *    the handle_* function
 * 4. Use "LOG" macro inside handle_* function to mark important output prints.
 *    Important output print is a string that might be used as an input for
 *    external scripts that parse the results of this tool.
 *
 * If in doubt, refer to the existing commands as the reference of the used
 * convention.
 */

static void handle_wifi_getRadioNumberOfEntries(int number_of_params, char **params);
static void handle_wifi_getRadioIfName(int number_of_params, char **params);
static void handle_wifi_getRadioOperatingFrequencyBand(int number_of_params, char **params);
static void handle_wifi_getRadioTransmitPower(int number_of_params, char **params);
static void handle_wifi_getRadioPossibleChannels(int number_of_params, char **params);
static void handle_wifi_getRadioChannels(int number_of_params, char **params);
static void handle_wifi_getSSIDNumberOfEntries(int number_of_params, char **params);
static void handle_wifi_getApName(int number_of_params, char **params);
static void handle_wifi_getSSIDEnable(int number_of_params, char **params);
static void handle_wifi_getSSIDNameStatus(int number_of_params, char **params);
static void handle_wifi_getSSIDName(int number_of_params, char **params);
static void handle_wifi_getSSIDRadioIndex(int number_of_params, char **params);
static void handle_wifi_getApAclDevices(int number_of_params, char **params);
static void handle_wifi_setRadioStatsEnable(int number_of_params, char **params);
static void handle_wifi_pushRadioChannel2(int number_of_params, char **params);
static void handle_wifi_delApAclDevices(int number_of_params, char **params);
static void handle_wifi_addApAclDevice(int number_of_params, char **params);
static void handle_wifi_startNeighborScan(int number_of_params, char **params);
static void handle_wifi_getRadioChannelStats(int number_of_params, char **params);
static void handle_wifi_getNeighboringWiFiStatus(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceStats(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceDiagnosticResult3(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceRxStatsResult(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceTxStatsResult(int number_of_params, char **params);
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
static void handle_wifi_pushMultiPskKeys(int number_of_params, char **params);
static void handle_wifi_getMultiPskKeys(int number_of_params, char **params);
#endif
static void handle_wifi_getRadioVapInfoMap(int number_of_params, char **params);
static void handle_wifi_createVAP(int number_of_params, char **params);
static void handle_wifi_getRadioOperatingParameters(int number_of_params, char **params);

static command_t commands_map[] = {
    { "wifi_getRadioNumberOfEntries", "", handle_wifi_getRadioNumberOfEntries, false},
    { "wifi_getRadioIfName", "radioIndex", handle_wifi_getRadioIfName, false},
    { "wifi_getRadioOperatingFrequencyBand", "radioIndex",
        handle_wifi_getRadioOperatingFrequencyBand, false},
    { "wifi_getRadioTransmitPower", "radioIndex", handle_wifi_getRadioTransmitPower, false},
    { "wifi_getRadioPossibleChannels", "radioIndex", handle_wifi_getRadioPossibleChannels, false},
    { "wifi_getRadioChannels", "radioIndex", handle_wifi_getRadioChannels, false},
    { "wifi_getSSIDNumberOfEntries", "", handle_wifi_getSSIDNumberOfEntries, false},
    { "wifi_getApName", "apIndex", handle_wifi_getApName, false},
    { "wifi_getSSIDEnable", "apIndex", handle_wifi_getSSIDEnable, false},
    { "wifi_getSSIDNameStatus", "apIndex", handle_wifi_getSSIDNameStatus, false},
    { "wifi_getSSIDName", "apIndex", handle_wifi_getSSIDName, false},
    { "wifi_getSSIDRadioIndex", "apIndex", handle_wifi_getSSIDRadioIndex, false},
    { "wifi_getApAclDevices", "apIndex", handle_wifi_getApAclDevices, false},
    { "wifi_setRadioStatsEnable", "radioIndex enable", handle_wifi_setRadioStatsEnable, false},
    { "wifi_pushRadioChannel2",
        "radioIndex channel ch_width_MHz csa_beacon_count", handle_wifi_pushRadioChannel2, false},
    { "wifi_delApAclDevices", "apIndex", handle_wifi_delApAclDevices, false},
    { "wifi_addApAclDevice", "apIndex mac", handle_wifi_addApAclDevice, false},
    { "wifi_startNeighborScan", "apIndex scan_mode dwell_time number_of_channels chan_list",
        handle_wifi_startNeighborScan, true},
    { "wifi_getRadioChannelStats", "radioIndex number_of_channels chan_list",
        handle_wifi_getRadioChannelStats, true},
    { "wifi_getNeighboringWiFiStatus", "radioIndex", handle_wifi_getNeighboringWiFiStatus, false},
    { "wifi_getApAssociatedDeviceStats", "apIndex mac", handle_wifi_getApAssociatedDeviceStats, false},
    { "wifi_getApAssociatedDeviceDiagnosticResult3", "apIndex", handle_wifi_getApAssociatedDeviceDiagnosticResult3, false},
    { "wifi_getApAssociatedDeviceRxStatsResult", "radioIndex mac", handle_wifi_getApAssociatedDeviceRxStatsResult, false},
    { "wifi_getApAssociatedDeviceTxStatsResult", "radioIndex mac", handle_wifi_getApAssociatedDeviceTxStatsResult, false},
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
    { "wifi_pushMultiPskKeys", "apIndex numKeys PSK1 KeyId1 ... PSKN KeyIdN", handle_wifi_pushMultiPskKeys, true},
    { "wifi_getMultiPskKeys", "apIndex", handle_wifi_getMultiPskKeys, false},
#endif
    { "wifi_getRadioVapInfoMap", "apIndex", handle_wifi_getRadioVapInfoMap, false},
    { "wifi_createVAP", "apIndex", handle_wifi_createVAP, false},
    { "handle_wifi_getRadioOperatingParameters", "radioIndex", handle_wifi_getRadioOperatingParameters, false},
};

#define COMMANDS_LEN (int)(sizeof(commands_map)/sizeof(commands_map[0]))

static void print_usage()
{
    int i;

    printf("\nRDK Wifi HAL tool - a command line RDK WiFi HAL API interface\n");
    printf("\nusage: wifi_hal_tool <command> [params...]\n"
           "\nAvailable commands:\n");

    for (i = 0; i < COMMANDS_LEN; i++)
    {
        printf("\n\t%s %s\n", commands_map[i].name, commands_map[i].params);
    }
    printf("\n");

    exit(0);
}

static void handle_wifi_getRadioNumberOfEntries(int number_of_params, char **params)
{
    INT ret;
    ULONG output = 0;

    ret = wifi_getRadioNumberOfEntries(&output);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioNumberOfEntries FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioNumberOfEntries OK ret=%d output=%d\n", (int)ret, (int)output);
}

static void handle_wifi_getRadioIfName(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR output_string[128];

    memset(output_string, 0, sizeof(output_string));

    radioIndex = atoi(params[0]);

    ret = wifi_getRadioIfName(radioIndex, output_string);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioIfName FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioIfName(%d) OK ret=%d output=>>%s<<\n", (int)radioIndex,
            ret, output_string);
}

static void handle_wifi_getRadioOperatingFrequencyBand(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR band[128];

    memset(band, 0, sizeof(band));

    radioIndex = atoi(params[0]);

    ret = wifi_getRadioOperatingFrequencyBand(radioIndex, band);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioOperatingFrequencyBand FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioOperatingFrequencyBand(%d) OK ret=%d band=>>%s<<\n", (int)radioIndex,
            ret, band);
}

static void handle_wifi_setRadioStatsEnable(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    BOOL enable = 0;

    radioIndex = atoi(params[0]);
    enable = atoi(params[1]);

    ret = wifi_setRadioStatsEnable(radioIndex, enable);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setRadioStatsEnable(%d, %d) FAILED ret=%d\n", radioIndex, enable,
                (int)ret);
        return;
    }

    LOG("wifi_setRadioStatsEnable(%d, %d) OK ret=%d\n", radioIndex, enable, (int)ret);
}

static void handle_wifi_pushRadioChannel2(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    UINT channel = 0;
    UINT channel_width_MHz = 0;
    UINT csa_beacon_count = 0;

    radioIndex = atoi(params[0]);
    channel = atoi(params[1]);
    channel_width_MHz = atoi(params[2]);
    csa_beacon_count = atoi(params[3]);

    ret = wifi_pushRadioChannel2(radioIndex, channel, channel_width_MHz,
            csa_beacon_count);
    if (ret != RETURN_OK)
    {
        LOG("wifi_pushRadioChannel2 FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_pushRadioChannel2(%d, %d, %d, %d) OK ret=%d\n", (int)radioIndex,
            channel, channel_width_MHz, csa_beacon_count, ret);
}

static void handle_wifi_getRadioTransmitPower(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    ULONG power = 0;

    radioIndex = atoi(params[0]);

    ret = wifi_getRadioTransmitPower(radioIndex, &power);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioTransmitPower FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioTransmitPower(%d) OK ret=%d power=%lu\n", (int)radioIndex,
            ret, power);
}

static void handle_wifi_getRadioPossibleChannels(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR channels[128];

    memset(channels, 0, sizeof(channels));
    radioIndex = atoi(params[0]);

    ret = wifi_getRadioPossibleChannels(radioIndex, channels);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioPossibleChannels FAILED ret=%d\n", (int)ret);
        return;
    }
    LOG("wifi_getRadioPossibleChannels(%d) OK ret=%d channels=>>%s<<\n", (int)radioIndex,
            ret, channels);
}

static void handle_wifi_getRadioChannels(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    const int MAP_SIZE = 24;
    wifi_channelMap_t map[MAP_SIZE];
    int i;
    char log_buffer[1024];
    int counter = 0;
    int bytes_left = sizeof(log_buffer) - 1;
    char *ptr = log_buffer;

    memset(map, 0, sizeof(map));
    memset(log_buffer, 0, sizeof(log_buffer));
    radioIndex = atoi(params[0]);

    ret = wifi_getRadioChannels(radioIndex, map, MAP_SIZE);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioChannels FAILED ret=%d\n", (int)ret);
        return;
    }
    for (i = 0; i < MAP_SIZE; i++)
    {
        counter = snprintf(ptr, bytes_left, " channel=%d:state=%d", map[i].ch_number,
                map[i].ch_state);
        bytes_left -= counter;
        ptr += counter;
    }
    LOG("wifi_getRadioChannels(%d) OK ret=%d map_size=%d%s\n", (int)radioIndex,
            ret, MAP_SIZE, log_buffer);
}

static void handle_wifi_getSSIDNumberOfEntries(int number_of_params, char **params)
{
    INT ret;
    ULONG output = 0;

    ret = wifi_getSSIDNumberOfEntries(&output);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getSSODNumberOfEntries FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getSSIDNumberOfEntries OK ret=%d output=%d\n", (int)ret, (int)output);
}

static void handle_wifi_getApName(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR ifname[64];

    memset(ifname, 0, sizeof(ifname));
    apIndex = atoi(params[0]);

    ret = wifi_getApName(apIndex, ifname);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApName FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApName(%d) OK ret=%d ifname=>>%s<<\n", (int)apIndex,
            ret, ifname);
}

static void handle_wifi_getSSIDEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getSSIDEnable(apIndex, &enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getSSIDEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getSSIDEnable(%d) OK ret=%d enabled=%d\n", (int)apIndex,
            ret, enabled);
}

static void handle_wifi_getSSIDNameStatus(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR ssid[128];

    memset(ssid, 0, sizeof(ssid));
    apIndex = atoi(params[0]);

    ret = wifi_getSSIDNameStatus(apIndex, ssid);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getSSIDNameStatus FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getSSIDNameStatus(%d) OK ret=%d ssid=>>%s<<\n", (int)apIndex,
            ret, ssid);
}

static void handle_wifi_getSSIDName(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR ssid[128];

    memset(ssid, 0, sizeof(ssid));
    apIndex = atoi(params[0]);

    ret = wifi_getSSIDName(apIndex, ssid);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getSSIDName FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getSSIDName(%d) OK ret=%d ssid=>>%s<<\n", (int)apIndex,
            ret, ssid);
}

static void handle_wifi_getSSIDRadioIndex(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    INT radioIndex = -1;

    apIndex = atoi(params[0]);

    ret = wifi_getSSIDRadioIndex(apIndex, &radioIndex);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getSSIDRadioIndex FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getSSIDRadioIndex(%d) OK ret=%d radioIndex=%d\n", (int)apIndex,
            ret, radioIndex);
}

static void handle_wifi_getApAclDevices(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;

    apIndex = atoi(params[0]);
#ifndef WIFI_HAL_VERSION_3_PHASE2
    CHAR acl_list[1024];

    memset(acl_list, 0, sizeof(acl_list));

    ret = wifi_getApAclDevices(apIndex, acl_list, sizeof(acl_list));
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAclDevices FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApAclDevices(%d) OK ret=%d acl_list=>>%s<<\n", (int)apIndex,
            ret, acl_list);
#else
    #define MAX_ACL_NUMBER 64
    UINT            acl_number;
    mac_address_t   acl_list[MAX_ACL_NUMBER] = {0};
    UINT            i;
    ret = wifi_getApAclDevices1(apIndex, acl_list, MAX_ACL_NUMBER, &acl_number);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAclDevices FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApAclDevices(%d) OK ret=%d acl_list=>>\n", (int)apIndex, ret);
    for (i = 0; i < acl_number; i++)
    {
        LOG("%hhx:%hhx:%hhx:%hhx:%hhx:%hhx\n", acl_list[i][0], acl_list[i][1], acl_list[i][2],
            acl_list[i][3], acl_list[i][4], acl_list[i][5]);
    }
    LOG("<<\n");
#endif
}

static void handle_wifi_delApAclDevices(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;

    apIndex = atoi(params[0]);

    ret = wifi_delApAclDevices(apIndex);
    if (ret != RETURN_OK)
    {
        LOG("wifi_delApAclDevices FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_delApAclDevices(%d) OK ret=%d\n", (int)apIndex, ret);
}

static void handle_wifi_addApAclDevice(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;

    apIndex = atoi(params[0]);
#ifndef WIFI_HAL_VERSION_3_PHASE2
    CHAR mac[128];

    memset(mac, 0, sizeof(mac));

    strncpy(mac, params[1], sizeof(mac) - 1);
#else
    mac_address_t mac = {0};
    sscanf(params[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
#endif

    ret = wifi_addApAclDevice(apIndex, mac);
    if (ret != RETURN_OK)
    {
        LOG("wifi_addApAclDevice FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_addApAclDevice(%d, >>%s<<) OK ret=%d\n", (int)apIndex,
            params[1], ret);
}

static void handle_wifi_startNeighborScan(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    wifi_neighborScanMode_t scan_mode;
    UINT number_of_channels;
    UINT chan_list[32];
    INT dwell_time;
    unsigned int i;
    char log_buffer[1024];
    int counter = 0;
    int bytes_left = sizeof(log_buffer) - 1;
    char *ptr = log_buffer;

    if (number_of_params < 5) print_usage();

    memset(log_buffer, 0, sizeof(log_buffer));
    memset(chan_list, 0, sizeof(chan_list));
    apIndex = atoi(params[0]);
    scan_mode = atoi(params[1]);
    dwell_time = atoi(params[2]);
    number_of_channels = atoi(params[3]);

    if (number_of_channels < 1 || number_of_channels > 32) print_usage();

    for (i = 0; i < number_of_channels; i++)
    {
        chan_list[i] = atoi(params[4 + i]);
        counter = snprintf(ptr, bytes_left, " channel=%d", chan_list[i]);
        bytes_left -= counter;
        ptr += counter;
    }

    ret = wifi_startNeighborScan(apIndex, scan_mode, dwell_time, number_of_channels, chan_list);
    if (ret != RETURN_OK)
    {
        LOG("wifi_startNeighborScan(%d, %d, %d, %d, %s) FAILED ret=%d\n", apIndex,
                scan_mode, dwell_time, number_of_channels, log_buffer, (int)ret);
        return;
    }

    LOG("wifi_startNeighborScan(%d, %d, %d, %d, %s) OK ret=%d\n", apIndex,
                scan_mode, dwell_time, number_of_channels, log_buffer, (int)ret);
}

static void handle_wifi_getRadioChannelStats(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    wifi_channelStats_t chans[32];
    INT number_of_channels;
    int i;
    char log_buffer[1024];
    int counter = 0;
    int bytes_left = sizeof(log_buffer) - 1;
    char *ptr = log_buffer;

    if (number_of_params < 3) print_usage();

    memset(log_buffer, 0, sizeof(log_buffer));
    memset(chans, 0, sizeof(chans));
    radioIndex = atoi(params[0]);
    number_of_channels = atoi(params[1]);

    if (number_of_channels < 1 || number_of_channels > 32) print_usage();

    for (i = 0; i < number_of_channels; i++)
    {
        chans[i].ch_number = atoi(params[2 + i]);
        chans[i].ch_in_pool = true;
        counter = snprintf(ptr, bytes_left, " channel=%d", chans[i].ch_number);
        bytes_left -= counter;
        ptr += counter;
    }

    ret = wifi_getRadioChannelStats(radioIndex, chans, number_of_channels);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioChannelStats(%d, chan_num=%d, %s) FAILED ret=%d\n", radioIndex,
                number_of_channels, log_buffer, (int)ret);
        return;
    }

    LOG("wifi_getRadioChannelStats(%d, chan_num=%d, %s) OK ret=%d\n", radioIndex,
                number_of_channels, log_buffer, (int)ret);

    for (i = 0; i < number_of_channels; i++)
    {
        LOG("channel=%d ch_utilization_total=%llu ch_utilization_busy=%llu "
            "ch_utilization_busy_tx=%llu ch_utilization_busy_self=%llu "
            "ch_utilization_busy_rx=%llu ch_utilization_busy_rx=%llu "
            "ch_utilization_busy_ext=%llu ch_noise=%ddbm ch_utilization=%d%%\n",
            chans[i].ch_number, chans[i].ch_utilization_total, chans[i].ch_utilization_busy,
            chans[i].ch_utilization_busy_tx, chans[i].ch_utilization_busy_self,
            chans[i].ch_utilization_busy_rx, chans[i].ch_utilization_busy_rx,
            chans[i].ch_utilization_busy_ext, chans[i].ch_noise,
            chans[i].ch_utilization);
    }
}

static void handle_wifi_getNeighboringWiFiStatus(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    unsigned int i;
    wifi_neighbor_ap2_t *neighbor_ap_array = NULL;
    UINT output_array_size = 0;

    radioIndex = atoi(params[0]);

#ifdef WIFI_HAL_VERSION_3_PHASE2
    ret = wifi_getNeighboringWiFiStatus(radioIndex, false, &neighbor_ap_array, &output_array_size);
#else
    ret = wifi_getNeighboringWiFiStatus(radioIndex, &neighbor_ap_array, &output_array_size);
#endif
    if (ret != RETURN_OK)
    {
        LOG("wifi_getNeighboringWiFiStatus(%d) FAILED ret=%d\n", radioIndex, (int)ret);
        return;
    }

    LOG("wifi_getNeighboringWiFiStatus(%d, output_array_size=%u) OK ret=%d\n", radioIndex,
            output_array_size, (int)ret);

    for (i = 0; i < output_array_size; i++)
    {
        LOG("ap_BSSID=>>%s<< ap_SSID=>>%s<< ap_Channel=%d ap_SignalStrength=%d "
            "ap_OperatingChannelBandwidth=>>%s<<\n", neighbor_ap_array[i].ap_BSSID,
             neighbor_ap_array[i].ap_SSID, neighbor_ap_array[i].ap_Channel,
             neighbor_ap_array[i].ap_SignalStrength,
             neighbor_ap_array[i].ap_OperatingChannelBandwidth);
    }

    free(neighbor_ap_array);
}

static void handle_wifi_getApAssociatedDeviceStats(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    mac_address_t mac;
    wifi_associated_dev_stats_t stats;
    ULLONG handle = 0;

    memset(&stats, 0, sizeof(stats));
    apIndex = atoi(params[0]);
    sscanf(params[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

    ret = wifi_getApAssociatedDeviceStats(apIndex, &mac, &stats, &handle);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAssociatedDeviceStats(%d, %02x:%02x:%02x:%02x:%02x:%02x) FAILED ret=%d\n", apIndex,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (int)ret);
        return;
    }

    LOG("wifi_getApAssociatedDeviceStats(%d, %02x:%02x:%02x:%02x:%02x:%02x) OK ret=%d handle=%llu\n", apIndex,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (int)ret, handle);

    LOG("cli_rx_bytes=%llu cli_tx_bytes=%llu cli_rx_frames=%llu cli_tx_frames=%llu cli_rx_retries=%llu "
        "cli_tx_retries=%llu cli_rx_errors=%llu cli_tx_errors=%llu cli_rx_rate=%f cli_tx_rate=%f\n",
        stats.cli_rx_bytes, stats.cli_tx_bytes, stats.cli_rx_frames, stats.cli_tx_frames,
        stats.cli_rx_retries, stats.cli_tx_retries, stats.cli_rx_errors, stats.cli_tx_errors,
        stats.cli_rx_rate, stats.cli_tx_rate);
}

static void handle_wifi_getApAssociatedDeviceDiagnosticResult3(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    wifi_associated_dev3_t *client_array;
    wifi_associated_dev3_t *client;
    UINT client_num;
    UINT i;

    apIndex = atoi(params[0]);

    ret = wifi_getApAssociatedDeviceDiagnosticResult3(apIndex, &client_array, &client_num);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAssociatedDeviceDiagnosticResult3(%d) FAILED ret=%d\n", apIndex, (int)ret);
        return;
    }

    LOG("wifi_getApAssociatedDeviceDiagnosticResult3(%d) OK ret=%d client_num=%u\n", apIndex,
            (int)ret, client_num);

    for (i = 0; i < client_num; i++)
    {
        client = &client_array[i];
        LOG("cli_MACAddress=%02x:%02x:%02x:%02x:%02x:%02x cli_AuthenticationState=%d cli_LastDataDownlinkRate=%u "
            "cli_LastDataUplinkRate=%u cli_SignalStrength=%d cli_Retransmissions=%u cli_Active=%d cli_OperatingStandard=>>%s<< "
            "cli_OperatingChannelBandwidth=>>%s<< cli_SNR=%d cli_InterferenceSources=>>%s<< cli_DataFramesSentAck=%lu "
            "cli_DataFramesSentNoAck=%lu cli_BytesSent=%lu cli_BytesReceived=%lu cli_RSSI=%d cli_MinRSSI=%d cli_MaxRSSI=%d "
            "cli_Disassociations=%u cli_AuthenticationFailures=%u cli_Associations=%llu cli_PacketsSent=%lu "
            "cli_PacketsReceived=%lu cli_ErrorsSent=%lu cli_RetransCount=%lu cli_FailedRetransCount=%lu cli_RetryCount=%lu "
            "cli_MultipleRetryCount=%lu cli_MaxDownlinkRate=%u cli_MaxUplinkRate=%u\n",
            client->cli_MACAddress[0], client->cli_MACAddress[1], client->cli_MACAddress[2],
            client->cli_MACAddress[3], client->cli_MACAddress[4], client->cli_MACAddress[5],
            client->cli_AuthenticationState, client->cli_LastDataDownlinkRate, client->cli_LastDataUplinkRate,
            client->cli_SignalStrength, client->cli_Retransmissions, client->cli_Active,
            client->cli_OperatingStandard, client->cli_OperatingChannelBandwidth, client->cli_SNR,
            client->cli_InterferenceSources, client->cli_DataFramesSentAck, client->cli_DataFramesSentNoAck,
            client->cli_BytesSent, client->cli_BytesReceived, client->cli_RSSI, client->cli_MinRSSI,
            client->cli_MaxRSSI, client->cli_Disassociations, client->cli_AuthenticationFailures,
            client->cli_Associations, client->cli_PacketsSent, client->cli_PacketsReceived,
            client->cli_ErrorsSent, client->cli_RetransCount, client->cli_FailedRetransCount,
            client->cli_RetryCount, client->cli_MultipleRetryCount, client->cli_MaxDownlinkRate,
            client->cli_MaxUplinkRate);
    }
}

static void handle_wifi_getApAssociatedDeviceRxStatsResult(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    mac_address_t mac;
    wifi_associated_dev_rate_info_rx_stats_t *stats_rx = NULL;
    UINT num_rx = 0;
    UINT i;
    ULLONG handle = 0;

    radioIndex = atoi(params[0]);
    sscanf(params[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

    ret = wifi_getApAssociatedDeviceRxStatsResult(radioIndex, &mac, &stats_rx, &num_rx, &handle);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAssociatedDeviceRxStatsResult(%d, %02x:%02x:%02x:%02x:%02x:%02x) FAILED ret=%d\n",
                radioIndex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (int)ret);
        return;
    }

    LOG("wifi_getApAssociatedDeviceRxStatsResult(%d, %02x:%02x:%02x:%02x:%02x:%02x) OK ret=%d "
        "num_rx=%u handle=%llu\n", radioIndex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (int)ret, num_rx, handle);

    for (i = 0; i < num_rx; i++)
    {
        LOG("nss=%u mcs=%u bw=%u flags=%llu bytes=%llu msdus=%llu mpdus=%llu "
            "ppdus=%llu retries=%llu rssi_combined=%u\n", stats_rx[i].nss, stats_rx[i].mcs,
            stats_rx[i].bw, stats_rx[i].flags, stats_rx[i].bytes, stats_rx[i].msdus,
            stats_rx[i].mpdus, stats_rx[i].ppdus, stats_rx[i].retries, stats_rx[i].rssi_combined);  // Note: missing "rssi_array"
    }

    free(stats_rx);
}

static void handle_wifi_getApAssociatedDeviceTxStatsResult(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    mac_address_t mac;
    wifi_associated_dev_rate_info_tx_stats_t *stats_tx = NULL;
    UINT num_tx = 0;
    UINT i;
    ULLONG handle = 0;

    radioIndex = atoi(params[0]);
    sscanf(params[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

    ret = wifi_getApAssociatedDeviceTxStatsResult(radioIndex, &mac, &stats_tx, &num_tx, &handle);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApAssociatedDeviceTxStatsResult(%d, %02x:%02x:%02x:%02x:%02x:%02x) FAILED ret=%d\n",
                radioIndex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (int)ret);
        return;
    }

    LOG("wifi_getApAssociatedDeviceTxStatsResult(%d, %02x:%02x:%02x:%02x:%02x:%02x) OK ret=%d "
        "num_rx=%u handle=%llu\n", radioIndex, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        (int)ret, num_tx, handle);

    for (i = 0; i < num_tx; i++)
    {
        LOG("nss=%u mcs=%u bw=%u flags=%llu bytes=%llu msdus=%llu mpdus=%llu "
            "ppdus=%llu retries=%llu attempts=%llu\n", stats_tx[i].nss, stats_tx[i].mcs,
            stats_tx[i].bw, stats_tx[i].flags, stats_tx[i].bytes, stats_tx[i].msdus,
            stats_tx[i].mpdus, stats_tx[i].ppdus, stats_tx[i].retries, stats_tx[i].attempts);
    }

    free(stats_tx);
}
#ifdef CONFIG_RDK_MULTI_PSK_SUPPORT
static void handle_wifi_pushMultiPskKeys(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    INT keysNumber;
    wifi_key_multi_psk_t *keys = NULL;
    int i;

    if ((number_of_params & 1) || number_of_params < 3)
    {
        print_usage();
        return;
    }

    apIndex = atoi(params[0]);
    keysNumber = atoi(params[1]);

    if (keysNumber != (number_of_params - 2) / 2)
    {
        print_usage();
        return;
    }

    keys = CALLOC(keysNumber, sizeof(wifi_key_multi_psk_t));

    for (i = 0; i < keysNumber; i++)
    {
        strncpy(keys[i].wifi_psk, params[i * 2 + 2], sizeof(keys[i].wifi_psk) - 1);
        strncpy(keys[i].wifi_keyId, params[i * 2 + 3], sizeof(keys[i].wifi_keyId) - 1);
    }

    ret = wifi_pushMultiPskKeys(apIndex, keys, keysNumber);
    if (ret != RETURN_OK)
    {
        LOG("wifi_pushMultiPskKeys FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_pushMultiPskKeys(%d) OK ret=%d\n", (int)apIndex, ret);
    FREE(keys);
}

static void handle_wifi_getMultiPskKeys(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    wifi_key_multi_psk_t keys[MAX_MULTI_PSK_KEYS];
    int i;

    memset(keys, 0, sizeof(keys));

    apIndex = atoi(params[0]);

    ret = wifi_getMultiPskKeys(apIndex, keys, MAX_MULTI_PSK_KEYS);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getMultiPskKeys(%d) FAILED ret=%d\n", apIndex, (int)ret);
        return;
    }

    for (i = 0; i < MAX_MULTI_PSK_KEYS; i++)
    {
         if (strlen(keys[i].wifi_keyId) && strlen(keys[i].wifi_psk))
         {
             LOG("keyID=%s key=%s\n", keys[i].wifi_keyId, keys[i].wifi_psk);
         }
    }
    LOG("wifi_getMultiPskKeys(%d) OK ret=%d\n", (int)apIndex, ret);
}
#endif
static bool get_vaps_map(wifi_radio_index_t index, wifi_vap_info_map_t *map)
{
    INT ret;
    unsigned int i;

    ret = wifi_getRadioVapInfoMap(index, map);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioVapInfoMap FAILED ret=%d\n", (int)ret);
        return false;
    }

    LOG("wifi_getRadioVapInfoMap(%d) OK ret=%d\n", (int)index, ret);
    printf("num_vaps=%u\n", map->num_vaps);
    for (i = 0; i < map->num_vaps; i++)
    {
        printf("%u vap_index=%d\n"
               "%u vap_name=%s\n"
               "%u radio_index=%u\n"
               "%u bridge_name=%s\n"
               "%u u.bss_info.ssid=%s\n"
               "%u u.bss_info.enabled=%d\n"
               "%u u.bss_info.showSsid=%d\n"
               "%u u.bss_info.isolation=%d\n"
               "%u u.bss_info.bssTransitionActivated=%d\n"
               "%u u.bss_info.nbrReportActivated=%d\n"
               "%u u.bss_info.security.u.key.key=%s\n"
               "%u u.bss_info.security.mode=%d\n"
               "%u u.bss_info.mac_filter_enable=%d\n"
               "%u u.bss_info.mac_filter_mode=%d\n"
               "%u u.bss_info.UAPSDEnabled=%d\n",
               i, map->vap_array[i].vap_index, i, map->vap_array[i].vap_name, i, map->vap_array[i].radio_index,
               i, map->vap_array[i].bridge_name, i, map->vap_array[i].u.bss_info.ssid, i, map->vap_array[i].u.bss_info.enabled,
               i, map->vap_array[i].u.bss_info.showSsid, i, map->vap_array[i].u.bss_info.isolation,
               i, map->vap_array[i].u.bss_info.bssTransitionActivated, i, map->vap_array[i].u.bss_info.nbrReportActivated,
               i, map->vap_array[i].u.bss_info.security.u.key.key, i, map->vap_array[i].u.bss_info.security.mode,
               i, map->vap_array[i].u.bss_info.mac_filter_enable, i, map->vap_array[i].u.bss_info.mac_filter_mode,
               i, map->vap_array[i].u.bss_info.UAPSDEnabled);
    }
    return true;
}

static void handle_wifi_getRadioVapInfoMap(int number_of_params, char **params)
{
    wifi_radio_index_t index;
    wifi_vap_info_map_t map = {0};

    index = atoi(params[0]);

    get_vaps_map(index, &map);
}

static void handle_wifi_createVAP(int number_of_params, char **params)
{
    wifi_radio_index_t index;
    wifi_vap_info_map_t map;
    INT ret;
    char line[1024] = {0};
    const char key_val_delim = '=';
    const char id_delim = ' ';
    char *ptr_key, *ptr_val;
    unsigned int vap_id;

    index = atoi(params[0]);

    if (!get_vaps_map(index, &map))
    {
        return;
    }

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        ptr_key = strchr(line, id_delim);
        if (ptr_key == NULL)
        {
            LOG("Invalid line, no vap id separator '%c'\n", id_delim);
            continue;
        }

        vap_id = atoi(line);
        ptr_key++;
        ptr_val = strchr(ptr_key, key_val_delim);
        if (ptr_val == NULL)
        {
            LOG("Invalid line, no vap key/value separator '%c'\n", key_val_delim);
            continue;
        }
        *ptr_val = '\0';
        ptr_val++;

        if (!strcmp(ptr_key, "u.bss_info.ssid"))
        {
            STRSCPY(map.vap_array[vap_id].u.bss_info.ssid, ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.enabled"))
        {
            map.vap_array[vap_id].u.bss_info.enabled = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.showSsid"))
        {
            map.vap_array[vap_id].u.bss_info.showSsid = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.isolation"))
        {
            map.vap_array[vap_id].u.bss_info.isolation = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.bssTransitionActivated"))
        {
            map.vap_array[vap_id].u.bss_info.bssTransitionActivated = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.nbrReportActivated"))
        {
            map.vap_array[vap_id].u.bss_info.nbrReportActivated = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.security.u.key.key"))
        {
            STRSCPY(map.vap_array[vap_id].u.bss_info.security.u.key.key, ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.security.mode"))
        {
            map.vap_array[vap_id].u.bss_info.security.mode = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.mac_filter_enable"))
        {
            map.vap_array[vap_id].u.bss_info.mac_filter_enable = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.mac_filter_mode"))
        {
            map.vap_array[vap_id].u.bss_info.mac_filter_mode = atoi(ptr_val);
        }
        else if (!strcmp(ptr_key, "u.bss_info.UAPSDEnabled"))
        {
            map.vap_array[vap_id].u.bss_info.UAPSDEnabled = atoi(ptr_val);
        }
    }

    ret = wifi_createVAP(index, &map);
    if (ret != RETURN_OK)
    {
        LOG("wifi_createVAP FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_createVAP(%d) OK ret=%d\n", index, ret);
}

static void handle_wifi_getRadioOperatingParameters(int number_of_params, char **params)
{
    wifi_radio_index_t index;
    wifi_radio_operationParam_t operationParam;
    INT ret;
    unsigned int i;

    index = atoi(params[0]);

    ret = wifi_getRadioOperatingParameters(index, &operationParam);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioOperatingParameters FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioOperatingParameters(%d) OK ret=%d\n", (int)index, ret);

    printf("enable=%s\n"
           "band=%d\n"
           "autoChannelEnabled=%s\n"
           "channel=%u\n"
           "channelWidth=0x%x\n"
           "variant=0x%x\n"
           "csa_beacon_count=%u\n"
           "countryCode=%d\n"
           "DCSEnabled=%s\n"
           "dtimPeriod=%u\n"
           "beaconInterval=%u\n"
           "operatingClass=%u\n"
           "basicDataTransmitRates=%u\n"
           "operationalDataTransmitRates=%u\n"
           "fragmentationThreshold=%u\n"
           "guardInterval=0x%x\n"
           "transmitPower=%u\n"
           "rtsThreshold=%u\n"
           "factoryResetSsid=%u\n"
           "radioStatsMeasuringRate=%u\n"
           "radioStatsMeasuringInterval=%u\n"
           "ctsProtection=%s\n"
           "obssCoex=%s\n"
           "stbcEnable=%s\n"
           "greenFieldEnable=%s\n"
           "userControl=%u\n"
           "adminControl=%u\n"
           "chanUtilThreshold=%u\n"
           "chanUtilSelfHealEnable=%s\n"
           "numSecondaryChannels=%u\n"
           "channelSecondary=",
           operationParam.enable ? "true" : "false", operationParam.band,
           operationParam.autoChannelEnabled ? "true" : "false", operationParam.channel,
           operationParam.channelWidth, operationParam.variant, operationParam.csa_beacon_count,
           operationParam.countryCode, operationParam.DCSEnabled ? "true" : "false", operationParam.dtimPeriod,
           operationParam.beaconInterval, operationParam.operatingClass, operationParam.basicDataTransmitRates,
           operationParam.operationalDataTransmitRates, operationParam.fragmentationThreshold,
           operationParam.guardInterval, operationParam.transmitPower, operationParam.rtsThreshold,
           operationParam.factoryResetSsid, operationParam.radioStatsMeasuringRate,
           operationParam.radioStatsMeasuringInterval, operationParam.ctsProtection ? "true" : "false",
           operationParam.obssCoex ? "true" : "false", operationParam.stbcEnable ? "true" : "false",
           operationParam.greenFieldEnable ? "true" : "false", operationParam.userControl,
           operationParam.adminControl, operationParam.chanUtilThreshold,
           operationParam.chanUtilSelfHealEnable ? "true" : "false", operationParam.numSecondaryChannels
           );
    for (i = 0; i < operationParam.numSecondaryChannels; i++)
    {
        printf("%u ", operationParam.channelSecondary[i]);
    }
    printf("\n");
}

static int get_number_of_params(const char *params)
{
    char *token;
    int i = 0;
    char str[MAX_PARAMS_LEN];

    memset(str, 0, sizeof(str));
    strncpy(str, params, sizeof(str) - 1);

    token = strtok(str, " ");

    while (token != NULL)
    {
        token = strtok(NULL, " ");
        i++;
    }

    return i;
}

static command_t* get_command(const char *name)
{
    int i;

    for (i = 0; i < COMMANDS_LEN; i++)
    {
        if (!strcmp(commands_map[i].name, name)) return &commands_map[i];
    }

    return NULL;
}

static void dispatch_cmd(int argc, char **argv)
{
    command_t *command = get_command(argv[1]);
    int number_of_params = argc - 2;  // skip process name and function name

    if (command == NULL) print_usage();
    if (number_of_params != get_number_of_params(command->params) &&
            !command->variable_number_of_params) print_usage();

    command->handler(number_of_params, &argv[2]);
}

int main(int argc,char **argv)
{
    if (argc < 2) print_usage();

    dispatch_cmd(argc, argv);

    return 0;
}
