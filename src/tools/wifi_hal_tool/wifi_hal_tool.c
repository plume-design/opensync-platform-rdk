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

#include <ccsp/wifi_hal.h>

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
static void handle_wifi_getRadioChannel(int number_of_params, char **params);
static void handle_wifi_getRadioEnable(int number_of_params, char **params);
static void handle_wifi_getRadioTransmitPower(int number_of_params, char **params);
static void handle_wifi_getRadioOperatingChannelBandwidth(int number_of_params, char **params);
static void handle_wifi_getRadioCountryCode(int number_of_params, char **params);
static void handle_wifi_getRadioStandard(int number_of_params, char **params);
static void handle_wifi_getRadioPossibleChannels(int number_of_params, char **params);
static void handle_wifi_getRadioChannels(int number_of_params, char **params);
static void handle_wifi_getSSIDNumberOfEntries(int number_of_params, char **params);
static void handle_wifi_getApName(int number_of_params, char **params);
static void handle_wifi_getSSIDEnable(int number_of_params, char **params);
static void handle_wifi_getApIsolationEnable(int number_of_params, char **params);
static void handle_wifi_getApSsidAdvertisementEnable(int number_of_params, char **params);
static void handle_wifi_getSSIDNameStatus(int number_of_params, char **params);
static void handle_wifi_getSSIDName(int number_of_params, char **params);
static void handle_wifi_getSSIDRadioIndex(int number_of_params, char **params);
static void handle_wifi_getBaseBSSID(int number_of_params, char **params);
static void handle_wifi_getNeighborReportActivation(int number_of_params, char **params);
static void handle_wifi_getBSSTransitionActivation(int number_of_params, char **params);
static void handle_wifi_getApSecurityModeEnabled(int number_of_params, char **params);
static void handle_wifi_getApSecurityKeyPassphrase(int number_of_params, char **params);
static void handle_wifi_getApMacAddressControlMode(int number_of_params, char **params);
static void handle_wifi_getApAclDevices(int number_of_params, char **params);
static void handle_wifi_setRadioStatsEnable(int number_of_params, char **params);
static void handle_wifi_pushRadioChannel2(int number_of_params, char **params);
static void handle_wifi_setApMacAddressControlMode(int number_of_params, char **params);
static void handle_wifi_delApAclDevices(int number_of_params, char **params);
static void handle_wifi_addApAclDevice(int number_of_params, char **params);
static void handle_wifi_setApSsidAdvertisementEnable(int number_of_params, char **params);
static void handle_wifi_setSSIDName(int number_of_params, char **params);
static void handle_wifi_setApSecurityModeEnabled(int number_of_params, char **params);
static void handle_wifi_setApSecurityKeyPassphrase(int number_of_params, char **params);
static void handle_wifi_setSSIDEnable(int number_of_params, char **params);
static void handle_wifi_setApIsolationEnable(int number_of_params, char **params);
static void handle_wifi_setNeighborReportActivation(int number_of_params, char **params);
static void handle_wifi_setBSSTransitionActivation(int number_of_params, char **params);
static void handle_wifi_applySSIDSettings(int number_of_params, char **params);
static void handle_wifi_startNeighborScan(int number_of_params, char **params);
static void handle_wifi_getRadioChannelStats(int number_of_params, char **params);
static void handle_wifi_getNeighboringWiFiStatus(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceStats(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceDiagnosticResult3(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceRxStatsResult(int number_of_params, char **params);
static void handle_wifi_getApAssociatedDeviceTxStatsResult(int number_of_params, char **params);
static void handle_wifi_pushMultiPskKeys(int number_of_params, char **params);
static void handle_wifi_getMultiPskKeys(int number_of_params, char **params);

static command_t commands_map[] = {
    { "wifi_getRadioNumberOfEntries", "", handle_wifi_getRadioNumberOfEntries, false},
    { "wifi_getRadioIfName", "radioIndex", handle_wifi_getRadioIfName, false},
    { "wifi_getRadioOperatingFrequencyBand", "radioIndex",
        handle_wifi_getRadioOperatingFrequencyBand, false},
    { "wifi_getRadioChannel", "radioIndex", handle_wifi_getRadioChannel, false},
    { "wifi_getRadioEnable", "radioIndex", handle_wifi_getRadioEnable, false},
    { "wifi_getRadioTransmitPower", "radioIndex", handle_wifi_getRadioTransmitPower, false},
    { "wifi_getRadioOperatingChannelBandwidth", "radioIndex", handle_wifi_getRadioOperatingChannelBandwidth, false},
    { "wifi_getRadioCountryCode", "radioIndex", handle_wifi_getRadioCountryCode, false},
    { "wifi_getRadioStandard", "radioIndex", handle_wifi_getRadioStandard, false},
    { "wifi_getRadioPossibleChannels", "radioIndex", handle_wifi_getRadioPossibleChannels, false},
    { "wifi_getRadioChannels", "radioIndex", handle_wifi_getRadioChannels, false},
    { "wifi_getSSIDNumberOfEntries", "", handle_wifi_getSSIDNumberOfEntries, false},
    { "wifi_getApName", "apIndex", handle_wifi_getApName, false},
    { "wifi_getSSIDEnable", "apIndex", handle_wifi_getSSIDEnable, false},
    { "wifi_getApIsolationEnable", "apIndex", handle_wifi_getApIsolationEnable, false},
    { "wifi_getApSsidAdvertisementEnable", "apIndex", handle_wifi_getApSsidAdvertisementEnable, false},
    { "wifi_getSSIDNameStatus", "apIndex", handle_wifi_getSSIDNameStatus, false},
    { "wifi_getSSIDName", "apIndex", handle_wifi_getSSIDName, false},
    { "wifi_getSSIDRadioIndex", "apIndex", handle_wifi_getSSIDRadioIndex, false},
    { "wifi_getBaseBSSID", "apIndex", handle_wifi_getBaseBSSID, false},
    { "wifi_getNeighborReportActivation", "apIndex", handle_wifi_getNeighborReportActivation, false},
    { "wifi_getBSSTransitionActivation", "apIndex", handle_wifi_getBSSTransitionActivation, false},
    { "wifi_getApSecurityModeEnabled", "apIndex", handle_wifi_getApSecurityModeEnabled, false},
    { "wifi_getApSecurityKeyPassphrase", "apIndex", handle_wifi_getApSecurityKeyPassphrase, false},
    { "wifi_getApMacAddressControlMode", "apIndex", handle_wifi_getApMacAddressControlMode, false},
    { "wifi_getApAclDevices", "apIndex", handle_wifi_getApAclDevices, false},
    { "wifi_setRadioStatsEnable", "radioIndex enable", handle_wifi_setRadioStatsEnable, false},
    { "wifi_pushRadioChannel2",
        "radioIndex channel ch_width_MHz csa_beacon_count", handle_wifi_pushRadioChannel2, false},
    { "wifi_setApMacAddressControlMode", "apIndex acl_mode", handle_wifi_setApMacAddressControlMode, false},
    { "wifi_delApAclDevices", "apIndex", handle_wifi_delApAclDevices, false},
    { "wifi_addApAclDevice", "apIndex mac", handle_wifi_addApAclDevice, false},
    { "wifi_setApSsidAdvertisementEnable", "apIndex enable", handle_wifi_setApSsidAdvertisementEnable, false},
    { "wifi_setSSIDName", "apIndex ssid", handle_wifi_setSSIDName, false},
    { "wifi_setApSecurityModeEnabled", "apIndex mode_str", handle_wifi_setApSecurityModeEnabled, false},
    { "wifi_setApSecurityKeyPassphrase", "apIndex passphrase", handle_wifi_setApSecurityKeyPassphrase, false},
    { "wifi_setSSIDEnable", "apIndex enable", handle_wifi_setSSIDEnable, false},
    { "wifi_setApIsolationEnable", "apIndex enable", handle_wifi_setApIsolationEnable, false},
    { "wifi_setNeighborReportActivation", "apIndex activate", handle_wifi_setNeighborReportActivation, false},
    { "wifi_setBSSTransitionActivation", "apIndex activate", handle_wifi_setBSSTransitionActivation, false},
    { "wifi_applySSIDSettings", "apIndex", handle_wifi_applySSIDSettings, false},
    { "wifi_startNeighborScan", "apIndex scan_mode dwell_time number_of_channels chan_list",
        handle_wifi_startNeighborScan, true},
    { "wifi_getRadioChannelStats", "radioIndex number_of_channels chan_list",
        handle_wifi_getRadioChannelStats, true},
    { "wifi_getNeighboringWiFiStatus", "radioIndex", handle_wifi_getNeighboringWiFiStatus, false},
    { "wifi_getApAssociatedDeviceStats", "apIndex mac", handle_wifi_getApAssociatedDeviceStats, false},
    { "wifi_getApAssociatedDeviceDiagnosticResult3", "apIndex", handle_wifi_getApAssociatedDeviceDiagnosticResult3, false},
    { "wifi_getApAssociatedDeviceRxStatsResult", "radioIndex mac", handle_wifi_getApAssociatedDeviceRxStatsResult, false},
    { "wifi_getApAssociatedDeviceTxStatsResult", "radioIndex mac", handle_wifi_getApAssociatedDeviceTxStatsResult, false},
    { "wifi_pushMultiPskKeys", "apIndex numKeys PSK1 KeyId1 ... PSKN KeyIdN", handle_wifi_pushMultiPskKeys, true},
    { "wifi_getMultiPskKeys", "apIndex", handle_wifi_getMultiPskKeys, false},
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

static void handle_wifi_getRadioChannel(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    ULONG channel = 0;

    radioIndex = atoi(params[0]);

    ret = wifi_getRadioChannel(radioIndex, &channel);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioChannel FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioChannel(%d) OK ret=%d channel=%lu\n", (int)radioIndex,
            ret, channel);
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

static void handle_wifi_getRadioEnable(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    BOOL enabled = 0;

    radioIndex = atoi(params[0]);

    ret = wifi_getRadioEnable(radioIndex, &enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioEnable(%d) OK ret=%d enabled=%d\n", (int)radioIndex,
            ret, enabled);
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

static void handle_wifi_getRadioOperatingChannelBandwidth(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR bandwidth[64];

    memset(bandwidth, 0, sizeof(bandwidth));
    radioIndex = atoi(params[0]);

    ret = wifi_getRadioOperatingChannelBandwidth(radioIndex, bandwidth);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioOperatingChannelBandwidth FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioOperatingChannelBandwidth(%d) OK ret=%d bandwidth=>>%s<<\n", (int)radioIndex,
            ret, bandwidth);
}

static void handle_wifi_getRadioCountryCode(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR country_code[64];

    memset(country_code, 0, sizeof(country_code));
    radioIndex = atoi(params[0]);

    ret = wifi_getRadioCountryCode(radioIndex, country_code);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioCountryCode FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioCountryCode(%d) OK ret=%d country=>>%s<<\n", (int)radioIndex,
            ret, country_code);
}

static void handle_wifi_getRadioStandard(int number_of_params, char **params)
{
    INT ret;
    INT radioIndex;
    CHAR standard[64];
    BOOL gonly = 0;
    BOOL nonly = 0;
    BOOL aconly = 0;

    memset(standard, 0, sizeof(standard));
    radioIndex = atoi(params[0]);

    ret = wifi_getRadioStandard(radioIndex, standard, &gonly, &nonly, &aconly);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getRadioStandard FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getRadioStandard(%d) OK ret=%d standard=>>%s<< "
            "gonly=%d nonly=%d aconly=%d\n", (int)radioIndex,
            ret, standard, gonly, nonly, aconly);
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

static void handle_wifi_getApIsolationEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getApIsolationEnable(apIndex, &enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApIsolationEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApIsolationEnable(%d) OK ret=%d enabled=%d\n", (int)apIndex,
            ret, enabled);
}

static void handle_wifi_getApSsidAdvertisementEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getApSsidAdvertisementEnable(apIndex, &enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApSsidAdvertisementEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApSsidAdvertisementEnable(%d) OK ret=%d enabled=%d\n", (int)apIndex,
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

static void handle_wifi_getBaseBSSID(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR mac[128];

    memset(mac, 0, sizeof(mac));

    apIndex = atoi(params[0]);

    ret = wifi_getBaseBSSID(apIndex, mac);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getBaseBSSID FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getBaseBSSID(%d) OK ret=%d mac=>>%s<<\n", (int)apIndex,
            ret, mac);
}

static void handle_wifi_getNeighborReportActivation(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL rrm = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getNeighborReportActivation(apIndex, &rrm);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getNeighborReportActivation FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getNeighborReportActivation(%d) OK ret=%d rrm=%d\n", (int)apIndex,
            ret, rrm);
}

static void handle_wifi_getBSSTransitionActivation(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL btm = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getBSSTransitionActivation(apIndex, &btm);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getBSSTransitionActivation FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getBSSTransitionActivation(%d) OK ret=%d btm=%d\n", (int)apIndex,
            ret, btm);
}

static void handle_wifi_getApSecurityModeEnabled(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR mode[128];

    memset(mode, 0, sizeof(mode));

    apIndex = atoi(params[0]);

    ret = wifi_getApSecurityModeEnabled(apIndex, mode);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApSecurityModeEnabled FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApSecurityModeEnabled(%d) OK ret=%d mode=>>%s<<\n", (int)apIndex,
            ret, mode);
}

static void handle_wifi_getApSecurityKeyPassphrase(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR passphrase[128];

    memset(passphrase, 0, sizeof(passphrase));

    apIndex = atoi(params[0]);

    ret = wifi_getApSecurityKeyPassphrase(apIndex, passphrase);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApSecurityKeyPassphrase FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApSecurityKeyPassphrase(%d) OK ret=%d passphrase=>>%s<<\n", (int)apIndex,
            ret, passphrase);
}

static void handle_wifi_getApMacAddressControlMode(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    INT acl_mode = 0;

    apIndex = atoi(params[0]);

    ret = wifi_getApMacAddressControlMode(apIndex, &acl_mode);
    if (ret != RETURN_OK)
    {
        LOG("wifi_getApMacAddressControlMode FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_getApMacAddressControlMode(%d) OK ret=%d acl_mode=%d\n", (int)apIndex,
            ret, acl_mode);
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

static void handle_wifi_setApMacAddressControlMode(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    INT acl_mode = 0;

    apIndex = atoi(params[0]);
    acl_mode = atoi(params[1]);

    ret = wifi_setApMacAddressControlMode(apIndex, acl_mode);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setApMacAddressControlMode FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setApMacAddressControlMode(%d, %d) OK ret=%d\n", (int)apIndex,
            acl_mode, ret);
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

static void handle_wifi_setApSsidAdvertisementEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);
    enabled = atoi(params[1]);

    ret = wifi_setApSsidAdvertisementEnable(apIndex, enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setApSsidAdvertisementEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setApSsidAdvertisementEnable(%d, %d) OK ret=%d\n", (int)apIndex,
            enabled, ret);
}

static void handle_wifi_setSSIDName(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR ssid[256];

    memset(ssid, 0, sizeof(ssid));

    apIndex = atoi(params[0]);
    strncpy(ssid, params[1], sizeof(ssid) - 1);

    ret = wifi_setSSIDName(apIndex, ssid);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setSSIDName FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setSSIDName(%d, >>%s<<) OK ret=%d\n", (int)apIndex,
            ssid, ret);
}

static void handle_wifi_setApSecurityModeEnabled(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR mode[256];

    memset(mode, 0, sizeof(mode));

    apIndex = atoi(params[0]);
    strncpy(mode, params[1], sizeof(mode) - 1);

    ret = wifi_setApSecurityModeEnabled(apIndex, mode);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setApSecurityModeEnabled FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setApSecurityModeEnabled(%d, >>%s<<) OK ret=%d\n", (int)apIndex,
            mode, ret);
}

static void handle_wifi_setApSecurityKeyPassphrase(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    CHAR passphrase[256];

    memset(passphrase, 0, sizeof(passphrase));

    apIndex = atoi(params[0]);
    strncpy(passphrase, params[1], sizeof(passphrase) - 1);

    ret = wifi_setApSecurityKeyPassphrase(apIndex, passphrase);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setApSecurityKeyPassphrase FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setApSecurityKeyPassphrase(%d, >>%s<<) OK ret=%d\n", (int)apIndex,
            passphrase, ret);
}

static void handle_wifi_setSSIDEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);
    enabled = atoi(params[1]);

    ret = wifi_setSSIDEnable(apIndex, enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setSSIDEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setSSIDEnable(%d, %d) OK ret=%d\n", (int)apIndex,
            enabled, ret);
}

static void handle_wifi_setApIsolationEnable(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL enabled = 0;

    apIndex = atoi(params[0]);
    enabled = atoi(params[1]);

    ret = wifi_setApIsolationEnable(apIndex, enabled);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setApIsolationEnable FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setApIsolationEnable(%d, %d) OK ret=%d\n", (int)apIndex,
            enabled, ret);
}

static void handle_wifi_setNeighborReportActivation(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL activate = 0;

    apIndex = atoi(params[0]);
    activate = atoi(params[1]);

    ret = wifi_setNeighborReportActivation(apIndex, activate);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setNeighborReportActivation FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setNeighborReportActivation(%d, %d) OK ret=%d\n", (int)apIndex,
            activate, ret);
}

static void handle_wifi_setBSSTransitionActivation(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;
    BOOL activate = 0;

    apIndex = atoi(params[0]);
    activate = atoi(params[1]);

    ret = wifi_setBSSTransitionActivation(apIndex, activate);
    if (ret != RETURN_OK)
    {
        LOG("wifi_setBSSTransitionActivation FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_setBSSTransitionActivation(%d, %d) OK ret=%d\n", (int)apIndex,
            activate, ret);
}

static void handle_wifi_applySSIDSettings(int number_of_params, char **params)
{
    INT ret;
    INT apIndex;

    apIndex = atoi(params[0]);

    ret = wifi_applySSIDSettings(apIndex);
    if (ret != RETURN_OK)
    {
        LOG("wifi_applySSIDSettings FAILED ret=%d\n", (int)ret);
        return;
    }

    LOG("wifi_applySSIDSettings(%d) OK ret=%d\n", (int)apIndex, ret);
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

    keys = calloc(keysNumber, sizeof(wifi_key_multi_psk_t));
    if (keys == NULL)
    {
        LOG("%s: Failed to allocate memory\n", __func__);
        return;
    }

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
