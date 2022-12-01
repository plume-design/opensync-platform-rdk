/*
Copyright (c) 2020, Plume Design Inc. All rights reserved.

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
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <ccsp/wifi_hal.h>
#include "memutil.h"

#define DEFAULT_PORT 5559
#define DEFAULT_OUTPUT_PORT 5558
#define DEFAULT_IP "0.0.0.0"
#define MAX_CONNS 5
#define PRI_os_macaddr_t        "%02X:%02X:%02X:%02X:%02X:%02X"
#define FMT_MAC(x) (x)[0], (x)[1], (x)[2], (x)[3], (x)[4], (x)[5]

#define LOG_PREFIX(prefix, ...) do { \
    memset(g_msg_buf, 0, sizeof(g_msg_buf)); \
    snprintf(g_msg_buf, sizeof(g_msg_buf), prefix __VA_ARGS__); \
    printf("bs_testd: %s", g_msg_buf); \
    if (g_output_sockfd != -1) { \
    sendto(g_output_sockfd, g_msg_buf, strlen(g_msg_buf), MSG_CONFIRM, (const struct sockaddr *)NULL, sizeof(struct sockaddr_in)); \
    } \
} while(0)
#define LOG(...) LOG_PREFIX("", __VA_ARGS__)
#define LOGE(...) LOG_PREFIX("[ERROR]: ", __VA_ARGS__)
#define LOGD(...) do { if (g_verbose) LOG_PREFIX("[DEBUG]: ", __VA_ARGS__); } while (0)
#define LOGI(...) LOG_PREFIX("[INFO]: ", __VA_ARGS__)
#define LOG_EVENT(...) LOG_PREFIX("[EVENT]: ", __VA_ARGS__)

static bool g_verbose = false;
static int g_output_sockfd = -1;
static char g_msg_buf[2048];

/* Below map must match wifi_steering_eventType_t
 * enum from wifi_hal.h"
 */
static char *g_event_type_map[9] = {
    "EVENT_UNKNOWN",
    "EVENT_PROBE_REQ",
    "EVENT_CLIENT_CONNECT",
    "EVENT_CLIENT_DISCONNECT",
    "EVENT_CLIENT_ACTIVITY",
    "EVENT_CHAN_UTILIZATION",
    "EVENT_RSSI_XING",
    "EVENT_RSSI",
    "EVENT_AUTH_FAIL"
};

typedef enum
{
    PARSE_SUCCESS,
    PARSE_EXIT,
    PARSE_ERROR
} parse_ret_t;

typedef struct
{
    bool daemon;
    char cmd_ip[128];
    int  cmd_port;
    bool output_to_socket;
    char output_ip[128];
    int  output_port;
} args_t;

static int setup_output_socket(const char *ip, int port)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        LOGE("output socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (!strcmp(ip, DEFAULT_IP))
    {
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        servaddr.sin_addr.s_addr = inet_addr(ip);
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        LOGE("connect failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int setup_input_socket(const char *ip, int port)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        LOGE("input socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    {
        LOGE("setsockopt SO_REUSEADDR failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    if (!strcmp(ip, DEFAULT_IP))
    {
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        servaddr.sin_addr.s_addr = inet_addr(ip);
    }

    servaddr.sin_port = htons(port);

    if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
    {
        LOGE("socket bind failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    if ((listen(sockfd, MAX_CONNS)) != 0)
    {
        LOGE("listen failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static void dump_event(UINT steeringgroupIndex, wifi_steering_event_t *event)
{
    char out[512];
    int bytes_left = sizeof(out) - 1;
    int counter = 0;
    char *ptr = out;
    wifi_steering_datarateInfo_t *datarate;
    wifi_steering_rrmCaps_t *rrmcaps;

    wifi_steering_eventType_t type = event->type;

    if (type >= sizeof(g_event_type_map))
    {
        LOGE("Incorrect event type: %d, groupIndex: %d\n", type, steeringgroupIndex);
        return;
    }

    memset(out, 0, sizeof(out));

    counter = snprintf(ptr, bytes_left, "type=%s\n\tgroupIndex=%d\n\tapIndex=%d\n\ttimestamp=%llu\n", g_event_type_map[event->type],
            steeringgroupIndex, event->apIndex, event->timestamp_ms);
    bytes_left -= counter;
    ptr += counter;

    switch (type)
    {
        case WIFI_STEERING_EVENT_PROBE_REQ:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" snr=%u, broadcast=%d, blocked=%d\n", FMT_MAC(event->data.probeReq.client_mac),
                    event->data.probeReq.rssi, event->data.probeReq.broadcast, event->data.probeReq.blocked);
            break;
        case WIFI_STEERING_EVENT_CLIENT_CONNECT:
            datarate = &event->data.connect.datarateInfo;
            rrmcaps = &event->data.connect.rrmCaps;
#ifdef WIFI_HAL_VERSION_3_PHASE2
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t"\n"
                    "\tisBTMSupported=%u\n\tisRRMSupported=%u\n\tbandsCap=%d\n\tmaxChwidth=%u\n\tmaxStreams=%u\n\tphyMode=%u\n"
                    "\tmaxMCS=%u\n\tmaxTxpower=%u\n\tisStaticSmps=%u\n\tisMUMimoSupported=%u\n\tlinkMeas=%d\n\tneighRpt=%d\n\tbcnRptPassive=%d\n"
                    "\tbcnRptActive=%d\n\tbcnRptTable=%d\n\tlciMeas=%d\n\tftmRangeRpt=%d\n",
                    FMT_MAC(event->data.probeReq.client_mac), event->data.connect.isBTMSupported, event->data.connect.isRRMSupported,
                    event->data.connect.bandsCap, datarate->maxChwidth, datarate->maxStreams, datarate->phyMode, datarate->maxMCS,
                    datarate->maxTxpower, datarate->isStaticSmps, datarate->isMUMimoSupported, rrmcaps->linkMeas, rrmcaps->neighRpt,
                    rrmcaps->bcnRptPassive, rrmcaps->bcnRptActive, rrmcaps->bcnRptTable, rrmcaps->lciMeas, rrmcaps->ftmRangeRpt
                   );
#else
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t"\n"
                    "\tisBTMSupported=%u\n\tisRRMSupported=%u\n\tbandCap2G=%d\n\tbandCap5G=%d\n\tmaxChwidth=%u\n\tmaxStreams=%u\n\tphyMode=%u\n"
                    "\tmaxMCS=%u\n\tmaxTxpower=%u\n\tisStaticSmps=%u\n\tisMUMimoSupported=%u\n\tlinkMeas=%d\n\tneighRpt=%d\n\tbcnRptPassive=%d\n"
                    "\tbcnRptActive=%d\n\tbcnRptTable=%d\n\tlciMeas=%d\n\tftmRangeRpt=%d\n",
                    FMT_MAC(event->data.probeReq.client_mac), event->data.connect.isBTMSupported, event->data.connect.isRRMSupported,
                    event->data.connect.bandCap2G, event->data.connect.bandCap5G, datarate->maxChwidth, datarate->maxStreams,
                    datarate->phyMode, datarate->maxMCS, datarate->maxTxpower, datarate->isStaticSmps, datarate->isMUMimoSupported,
                    rrmcaps->linkMeas, rrmcaps->neighRpt, rrmcaps->bcnRptPassive, rrmcaps->bcnRptActive, rrmcaps->bcnRptTable,
                    rrmcaps->lciMeas, rrmcaps->ftmRangeRpt
                    );
#endif
            break;
        case WIFI_STEERING_EVENT_CLIENT_DISCONNECT:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" reason=%u, source=%d type=%d\n", FMT_MAC(event->data.disconnect.client_mac),
                    event->data.disconnect.reason, event->data.disconnect.source, event->data.disconnect.type);
            break;
        case WIFI_STEERING_EVENT_CLIENT_ACTIVITY:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" active=%d\n", FMT_MAC(event->data.activity.client_mac),
                    event->data.activity.active);
            break;
        case WIFI_STEERING_EVENT_CHAN_UTILIZATION:
            snprintf(ptr, bytes_left, "\tutilization=%u\n", event->data.chanUtil.utilization);
            break;
        case WIFI_STEERING_EVENT_RSSI_XING:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" snr=%u inactive=%d high=%d low=%d\n", FMT_MAC(event->data.rssiXing.client_mac),
                    event->data.rssiXing.rssi, event->data.rssiXing.inactveXing, event->data.rssiXing.highXing, event->data.rssiXing.lowXing);
            break;
        case WIFI_STEERING_EVENT_RSSI:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" snr=%u\n", FMT_MAC(event->data.rssi.client_mac),
                    event->data.rssi.rssi);
            break;
        case WIFI_STEERING_EVENT_AUTH_FAIL:
            snprintf(ptr, bytes_left, "\tmac="PRI_os_macaddr_t" snr=%u reason=%u bsBlocked=%d bsRejected=%d\n", FMT_MAC(event->data.authFail.client_mac),
                    event->data.authFail.rssi, event->data.authFail.reason, event->data.authFail.bsBlocked, event->data.authFail.bsRejected);

            break;
        default:
            LOGE("Incorrect event: %d, groupIndex: %d\n", type, steeringgroupIndex);
            return;
    }

    LOG_EVENT("%s\n", out);
}

static void set_cfg(wifi_steering_apConfig_t *cfg, char *token, char **params,
        char *cfg_buf, size_t bufsz)
{
        LOGD("apIndex=%s\n", token);
        cfg->apIndex = (INT)strtol(token, NULL, 10);
        token = strsep(params, ";");
        LOGD("utilCheckIntervalSec=%s\n", token);
        cfg->utilCheckIntervalSec = (UINT)strtol(token, NULL, 10);
        token = strsep(params, ";");
        LOGD("utilAvgCount=%s\n", token);
        cfg->utilAvgCount = (UINT)strtol(token, NULL, 10);
        token = strsep(params, ";");
        LOGD("inactCheckIntervalSec=%s\n", token);
        cfg->inactCheckIntervalSec = (UINT)strtol(token, NULL, 10);
        token = strsep(params, ";");
        LOGD("inactCheckThresholdSec=%s\n", token);
        cfg->inactCheckThresholdSec = (UINT)strtol(token, NULL, 10);

        snprintf(cfg_buf, bufsz, "apIndex=%d, utilCheckIntervalSec=%u "
                "utilAvgCount=%u, inactCheckIntervalSec=%u, "
                "inactCheckThresholdSec=%u", cfg->apIndex, cfg->utilCheckIntervalSec,
                cfg->utilAvgCount, cfg->inactCheckIntervalSec, cfg->inactCheckThresholdSec);
}

#ifdef WIFI_HAL_VERSION_3_PHASE2
static bool handle_set_group(char *params)
{
    #define MAX_AP_CFG_NUMBER 20
    char *token;
    UINT steeringgroupIndex;
    char buf[MAX_AP_CFG_NUMBER][128];
    wifi_steering_apConfig_t ap_cfg[MAX_AP_CFG_NUMBER] = {0};
    UINT ap_cfg_number;
    UINT i;

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    for (ap_cfg_number = 0; ap_cfg_number < MAX_AP_CFG_NUMBER; ap_cfg_number++)
    {
        token = strsep(&params, ";");
        if (params == NULL) break;

        if (strcmp(token, "NULL") == 0) {
            STRSCPY(buf[ap_cfg_number], "(NULL)");
        } else {
            set_cfg(&ap_cfg[ap_cfg_number], token, &params, buf[ap_cfg_number], sizeof(buf[ap_cfg_number]));
        }
    }

    LOGI("wifi_steering_setGroup()\n\tsteeringgroupIndex=%u\n\t", steeringgroupIndex);

    for (i = 0; i < ap_cfg_number; i++)
    {
        LOGI("cfg: %s\n", buf[i]);
    }

    return wifi_steering_setGroup(steeringgroupIndex, i, ap_cfg);
}
#else
static bool handle_set_group(char *params)
{
    char *token;
    UINT steeringgroupIndex;
    wifi_steering_apConfig_t cfg_2;
    wifi_steering_apConfig_t cfg_5;
    char cfg_2_buf[128];
    char cfg_5_buf[128];
    wifi_steering_apConfig_t *cfg_2_ptr = NULL;
    wifi_steering_apConfig_t *cfg_5_ptr = NULL;

    memset(cfg_2_buf, 0, sizeof(cfg_2_buf));
    memset(cfg_5_buf, 0, sizeof(cfg_5_buf));

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    if (!strcmp(token, "NULL")) strncpy(cfg_2_buf, "(NULL)", sizeof(cfg_2_buf));
    else
    {
        set_cfg(&cfg_2, token, &params, cfg_2_buf, sizeof(cfg_2_buf));
        cfg_2_ptr = &cfg_2;
    }

    token = strsep(&params, ";");
    if (!strcmp(token, "NULL")) strncpy(cfg_5_buf, "(NULL)", sizeof(cfg_5_buf));
    else
    {
        set_cfg(&cfg_5, token, &params, cfg_5_buf, sizeof(cfg_5_buf));
        cfg_5_ptr = &cfg_5;
    }

    LOGI("wifi_steering_setGroup()\n\tsteeringgroupIndex=%u\n\tcfg_2: %s\n\tcfg_5: %s\n",
        steeringgroupIndex, cfg_2_buf, cfg_5_buf);

    return wifi_steering_setGroup(steeringgroupIndex, cfg_2_ptr, cfg_5_ptr);
}
#endif

static void str_to_mac_addr(const char *mac_str, mac_address_t mac)
{
    sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2],
           &mac[3], &mac[4], &mac[5]);
}

static bool handle_set_client(char *params)
{
    char *token;
    UINT steeringgroupIndex;
    INT apIndex;
    mac_address_t client_mac;
    wifi_steering_clientConfig_t config;

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, client_mac);

    token = strsep(&params, ";");
    LOGD("rssiProbeHWM=%s\n", token);
    config.rssiProbeHWM = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiProbeLWM=%s\n", token);
    config.rssiProbeLWM = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiAuthHWM=%s\n", token);
    config.rssiAuthHWM = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiAuthLWM=%s\n", token);
    config.rssiAuthLWM = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiInactXing=%s\n", token);
    config.rssiInactXing = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiHighXing=%s\n", token);
    config.rssiHighXing = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rssiLowXing=%s\n", token);
    config.rssiLowXing = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("authRejectReason=%s\n", token);
    config.authRejectReason = (UINT)strtol(token, NULL, 10);

    LOGI("wifi_steering_clientSet()\n"
            "\tsteeringgroupIndex=%u\n\tapIndex=%u\n\tclient_mac="PRI_os_macaddr_t"\n"
            "\trssiProbeHWM=%u\n\trssiProbeLWM=%u\n\trssiAuthHWM=%u\n"
            "\trssiAuthLWM=%u\n\trssiInactXing=%u\n\trssiHighXing=%u\n"
            "\trssiLowXing=%u\n\tauthRejectReason=%u\n", steeringgroupIndex,
            apIndex, FMT_MAC(client_mac), config.rssiProbeHWM, config.rssiProbeLWM,
            config.rssiAuthHWM, config.rssiAuthLWM, config.rssiInactXing,
            config.rssiHighXing, config.rssiLowXing, config.authRejectReason);
    return wifi_steering_clientSet(steeringgroupIndex, apIndex, client_mac, &config);
}

static bool handle_setBTMRequest(char *params)
{
    char *token;
    INT apIndex;
    mac_address_t peerMac;
    wifi_BTMRequest_t request;
    int i;
    char buf[1024];
    char *ptr = buf;
    int counter = 0;
    int bytes_left = sizeof(buf);

    memset(&request, 0, sizeof(request));

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);
    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, peerMac);

    token = strsep(&params, ";");
    LOGD("token=%s\n", token);
    request.token = (UCHAR)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("requestMode=%s\n", token);
    request.requestMode = (UCHAR)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("timer=%s\n", token);
    request.timer= (USHORT)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("validityInterval=%s\n", token);
    request.validityInterval = (UCHAR)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("termDuration.tsf=%s\n", token);
    request.termDuration.tsf = (ULONG)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("termDuration.duration=%s\n", token);
    request.termDuration.duration = (USHORT)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("numCandidates=%s\n", token);
    request.numCandidates = (UCHAR)strtol(token, NULL, 0);

    for (i = 0; i < request.numCandidates; i++)
    {
        token = strsep(&params, ";");
        LOGD("bssid[%d]=%s\n", i, token);
        str_to_mac_addr(token, request.candidates[i].bssid);

        token = strsep(&params, ";");
        LOGD("info[%d]=%s\n", i, token);
        request.candidates[i].info = (UINT)strtol(token, NULL, 0);

        token = strsep(&params, ";");
        LOGD("opClass[%d]=%s\n", i, token);
        request.candidates[i].opClass = (UCHAR)strtol(token, NULL, 0);

        token = strsep(&params, ";");
        LOGD("channel[%d]=%s\n", i, token);
        request.candidates[i].channel = (UCHAR)strtol(token, NULL, 0);

        token = strsep(&params, ";");
        LOGD("phyTable[%d]=%s\n", i, token);
        request.candidates[i].phyTable = (UCHAR)strtol(token, NULL, 0);

        // TODO: missing preference-present
        token = strsep(&params, ";");
        LOGD("preference[%d]=%s\n", i, token);
        request.candidates[i].bssTransitionCandidatePreference.preference = (UCHAR)strtol(token, NULL, 0);
    }

    memset(buf, 0, sizeof(buf));
    counter = snprintf(ptr, bytes_left,
            "\tapIndex=%u\n\tpeerMac="PRI_os_macaddr_t"\n"
            "\ttoken=0x%x\n\trequestMode=0x%x\n\ttimer=0x%x\n"
            "\tvalidityInterval=0x%x\n\ttermDuration.tsf=0x%lx\n"
            "\ttermDuration.duration=0x%x\n\tnumCandidates=%d\n",
            apIndex, FMT_MAC(peerMac), request.token, request.requestMode,
            request.timer, request.validityInterval, request.termDuration.tsf,
            request.termDuration.duration, request.numCandidates);
    bytes_left -= counter;
    ptr += counter;
    for (i = 0; i < request.numCandidates; i++)
    {
        counter = snprintf(ptr, bytes_left, "\tcandidates[%d].bssid="PRI_os_macaddr_t"\n"
                "\tcandidates[%d].info=0x%x\n\tcandidates[%d].opClass=%x\n"
                "\tcandidates[%d].channel=%d\n\tcandidates[%d].phyTable=%x\n"
                "\tcandidates[%d].bssTransitionCandidatePreference.preference=%x\n",
                i, FMT_MAC(request.candidates[i].bssid), i, request.candidates[i].info,
                i, request.candidates[i].opClass, i, request.candidates[i].channel, i,
                request.candidates[i].phyTable, i,
                request.candidates[i].bssTransitionCandidatePreference.preference);
        bytes_left -= counter;
        ptr += counter;
    }

    LOGI("wifi_setBTMRequest() %s\n", buf);
#ifdef WIFI_HAL_VERSION_3_PHASE2
    return wifi_setBTMRequest(apIndex, peerMac, &request);
#else
    return wifi_setBTMRequest(apIndex, (CHAR *)peerMac, &request);
#endif
}

static bool handle_setRMBeaconRequest(char *params)
{
    char *token;
    INT apIndex;
    mac_address_t peerMac;
    wifi_BeaconRequest_t request;
    UCHAR out_DialogToken;

    memset(&request, 0, sizeof(request));

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, peerMac);

    token = strsep(&params, ";");
    LOGD("out_DialogToken=%s\n", token);
    out_DialogToken = (UCHAR)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("opClass=%s\n", token);
    request.opClass = (UCHAR)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("mode=%s\n", token);
    request.mode = (UCHAR)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("channel=%s\n", token);
    request.channel = (UCHAR)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("randomizationInterval=%s\n", token);
    request.randomizationInterval = (USHORT)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("duration=%s\n", token);
    request.duration = (USHORT)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("ssidPresent=%s\n", token);
    request.ssidPresent = (BOOL)strtol(token, NULL, 0);

    token = strsep(&params, ";");
    LOGD("bssid=%s\n", token);
    str_to_mac_addr(token, request.bssid);

    LOGI("wifi_setRMBeaconRequest()\n\tapIndex=%d\n\tpeerMac="PRI_os_macaddr_t"\n"
            "\tout_DialogToken=%d\n\trequest.opClass=0x%x\n\trequest.mode=0x%x\n"
            "\trequest.channel=%d\n\trequest.randomizationInterval=0x%x\n"
            "\trequest.duration=0x%x\n\t, request.ssidPresent=%d\n"
            "\trequest.bssid="PRI_os_macaddr_t"\n", apIndex, FMT_MAC(peerMac),
            out_DialogToken, request.opClass, request.mode, request.channel,
            request.randomizationInterval, request.duration, request.ssidPresent,
            FMT_MAC(request.bssid));
#ifdef WIFI_HAL_VERSION_3_PHASE2
    return wifi_setRMBeaconRequest(apIndex, peerMac, &request, &out_DialogToken);
#else
    return wifi_setRMBeaconRequest(apIndex, (CHAR *)&peerMac, &request, &out_DialogToken);
#endif
}

static bool handle_clientDisconnect(char *params)
{
    char *token;
    UINT steeringgroupIndex;
    INT apIndex;
    mac_address_t client_mac;
    wifi_disconnectType_t type;
    UINT reason;

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, client_mac);

    token = strsep(&params, ";");
    LOGD("type=%s\n", token);
    type = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("reason=%s\n", token);
    reason = (UINT)strtol(token, NULL, 10);

    LOGI("wifi_steering_clientDisconnect()\n\tsteeringgroupIndex=%d\n"
            "\tapIndex=%d\n\tclient_mac="PRI_os_macaddr_t"\n"
            "\ttype=%d\n\treason=%d\n", steeringgroupIndex, apIndex,
            FMT_MAC(client_mac), type, reason);
    return wifi_steering_clientDisconnect(steeringgroupIndex, apIndex, client_mac, type, reason);
}

static bool handle_clientMeasure(char *params)
{
    char *token;
    UINT steeringgroupIndex;
    INT apIndex;
    mac_address_t client_mac;

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, client_mac);

    LOGI("wifi_steering_clientMeasure()\n\tsteeringgroupIndex=%d\n"
            "\tapIndex=%d\n\tclient_mac="PRI_os_macaddr_t"\n", steeringgroupIndex, apIndex,
            FMT_MAC(client_mac));
    return wifi_steering_clientMeasure(steeringgroupIndex, apIndex, client_mac);
}

static bool handle_clientRemove(char *params)
{
    char *token;
    UINT steeringgroupIndex;
    INT apIndex;
    mac_address_t client_mac;

    token = strsep(&params, ";");
    LOGD("steeringgroupIndex=%s\n", token);
    steeringgroupIndex = (UINT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("mac=%s\n", token);
    str_to_mac_addr(token, client_mac);

    LOGI("wifi_steering_clientRemove()\n\tsteeringgroupIndex=%d\n"
            "\tapIndex=%d\n\tclient_mac="PRI_os_macaddr_t"\n", steeringgroupIndex, apIndex,
            FMT_MAC(client_mac));
    return wifi_steering_clientRemove(steeringgroupIndex, apIndex, client_mac);
}

static bool ssid_index_to_radio_idx(UINT ssid_index, INT *radio_idx)
{
    if (wifi_getSSIDRadioIndex(ssid_index, radio_idx) != RETURN_OK)
    {
        LOGE("wifi_getSSIDRadioIndex() FAILED ssid_index=%d", ssid_index);
        return false;
    }
    return true;
}

static bool ssid_index_to_vap_info(UINT ssid_index, INT radio_idx, wifi_vap_info_map_t *map, wifi_vap_info_t **vap_info)
{
    UINT i;

    memset(map, 0, sizeof(wifi_vap_info_map_t));

    if (wifi_getRadioVapInfoMap(radio_idx, map) == RETURN_OK)
    {
        for (i = 0; i < map->num_vaps; i++)
        {
            if (map->vap_array[i].vap_index == ssid_index)
            {
                *vap_info = &map->vap_array[i];
                return true;
            }
        }
    }

    LOGE("Cannot find vap_info for ssid_index %d", ssid_index);
    return false;
}

static bool handle_get_bssTransitionActivated(char *params)
{
    char *token;
    INT apIndex;
    INT radio_idx;
    wifi_vap_info_map_t map = {0};
    wifi_vap_info_t *vap_info = NULL;

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    LOGI("get_bssTransitionActivated\n\tapIndex=%d\n", apIndex);

    if (!ssid_index_to_radio_idx((UINT)apIndex, &radio_idx)) return true;

    if (!ssid_index_to_vap_info((UINT)apIndex, radio_idx, &map, &vap_info)) return true;

    LOGI("BTM = %d (%s)\n", vap_info->u.bss_info.bssTransitionActivated,
        vap_info->u.bss_info.bssTransitionActivated ? "enabled" : "disabled");

    return false;
}

static bool handle_get_nbrReportActivated(char *params)
{
    char *token;
    INT apIndex;
    INT radio_idx;
    wifi_vap_info_map_t map = {0};
    wifi_vap_info_t *vap_info = NULL;

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    LOGI("get_bssTransitionActivated\n\tapIndex=%d\n", apIndex);

    if (!ssid_index_to_radio_idx((UINT)apIndex, &radio_idx)) return true;

    if (!ssid_index_to_vap_info((UINT)apIndex, radio_idx, &map, &vap_info)) return true;

    LOGI("RRM = %d (%s)\n", vap_info->u.bss_info.nbrReportActivated,
        vap_info->u.bss_info.nbrReportActivated ? "enabled" : "disabled");

    return false;
}

static bool handle_bssTransitionActivated(char *params)
{
    char *token;
    INT apIndex;
    INT radio_idx;
    BOOL btm;
    wifi_vap_info_map_t map = {0};
    wifi_vap_info_t *vap_info = NULL;

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("btm=%s\n", token);
    btm = (INT)strtol(token, NULL, 10);

    if (!ssid_index_to_radio_idx((UINT)apIndex, &radio_idx)) return true;

    if (!ssid_index_to_vap_info((UINT)apIndex, radio_idx, &map, &vap_info)) return true;

    vap_info->u.bss_info.bssTransitionActivated = btm;

    return wifi_createVAP((wifi_radio_index_t)radio_idx, &map);
}

static bool handle_nbrReportActivated(char *params)
{
    char *token;
    INT apIndex;
    INT radio_idx;
    BOOL rrm;
    wifi_vap_info_map_t map = {0};
    wifi_vap_info_t *vap_info = NULL;

    token = strsep(&params, ";");
    LOGD("apIndex=%s\n", token);
    apIndex = (INT)strtol(token, NULL, 10);

    token = strsep(&params, ";");
    LOGD("rrm=%s\n", token);
    rrm = (INT)strtol(token, NULL, 10);

    if (!ssid_index_to_radio_idx((UINT)apIndex, &radio_idx)) return true;

    if (!ssid_index_to_vap_info((UINT)apIndex, radio_idx, &map, &vap_info)) return true;

    vap_info->u.bss_info.nbrReportActivated = rrm;

    return wifi_createVAP((wifi_radio_index_t)radio_idx, &map);
}

static int dispatch_cmd(const char *cmd_name, char *params)
{
    LOGD("calling %s\n", cmd_name);

    if (!strcmp(cmd_name, "wifi_steering_eventRegister"))
    {
        LOGI("wifi_steering_eventRegister()\n");
        return wifi_steering_eventRegister(dump_event);
    }

    if (!strcmp(cmd_name, "wifi_steering_eventUnregister"))
    {
        LOGI("wifi_steering_eventUnregister()\n");
        return wifi_steering_eventUnregister();
    }

    if (!strcmp(cmd_name, "wifi_steering_setGroup"))
    {
        return handle_set_group(params);
    }

    if (!strcmp(cmd_name, "wifi_steering_clientSet"))
    {
        return handle_set_client(params);
    }

    if (!strcmp(cmd_name, "wifi_setBTMRequest"))
    {
        return handle_setBTMRequest(params);
    }

    if (!strcmp(cmd_name, "wifi_setRMBeaconRequest"))
    {
        return handle_setRMBeaconRequest(params);
    }

    if (!strcmp(cmd_name, "wifi_steering_clientDisconnect"))
    {
        return handle_clientDisconnect(params);
    }

    if (!strcmp(cmd_name, "wifi_steering_clientMeasure"))
    {
        return handle_clientMeasure(params);
    }

    if (!strcmp(cmd_name, "wifi_steering_clientRemove"))
    {
        return handle_clientRemove(params);
    }

    if (!strcmp(cmd_name, "get_bssTransitionActivated"))
    {
        return handle_get_bssTransitionActivated(params);
    }

    if (!strcmp(cmd_name, "get_nbrReportActivated"))
    {
        return handle_get_nbrReportActivated(params);
    }

    if (!strcmp(cmd_name, "bssTransitionActivated"))
    {
        return handle_bssTransitionActivated(params);
    }

    if (!strcmp(cmd_name, "nbrReportActivated"))
    {
        return handle_nbrReportActivated(params);
    }

    LOGE("unknown cmd: >>%s<<\n", cmd_name);
    return false;
}

static bool handle_cmd(const char *cmd)
{
    char *buf;
    char *handle;
    char *token;
    bool ret = false;

    buf = STRDUP(cmd);
    handle = buf;

    token = strsep(&buf, ";");

    ret = dispatch_cmd(token, buf);

    FREE(handle);
    return ret;
}

static bool send_ack(int connfd, const char *status)
{
    if (write(connfd, status, strlen(status)) == -1)
    {
        LOGE("write ACK %s failed: %s\n", status, strerror(errno));
        close(connfd);
        return false;
    }

    return true;
}

static parse_ret_t parse_cmd(int connfd)
{
    char buf[1024];
    ssize_t bytes = 0;
    ssize_t total_bytes = 0;
    bool incomplete = false;
    char *ptr = buf;

    while (true)
    {
        if (!incomplete) memset(buf, 0, sizeof(buf));

        bytes = read(connfd, ptr, sizeof(buf) - total_bytes);
        if (bytes == -1)
        {
            LOGE("read failed: %s\n", strerror(errno));
            return PARSE_ERROR;
        }

        if (bytes == 0)
        {
            LOGD("connection closed\n");
            close(connfd);
            break;
        }

        total_bytes += bytes;
        LOGD("total_bytes = %d\n", total_bytes);

        if (total_bytes >= (ssize_t)sizeof(buf) && buf[sizeof(buf) - 1] != '\0')
        {
            LOGE("Malformed command\n");
            close(connfd);
            return PARSE_ERROR;
        }

        if (buf[total_bytes] != '\0')
        {
            // We're reading until we get a full null-terminated string.
            // Otherwise we're blocking (on purpose).
            // in case of client malfunction this loop needs to be interrupted by a signal.
            // Non-blocking variant may be implemented in the future.
            LOGD("incomplete, bytes = %d , buf = >>%s<<\n", bytes, buf);
            incomplete = true;
            ptr += bytes;
            continue;
        }
        incomplete = false;

        // Special command to stop the daemon
        if (!strcmp(buf, "exit"))
        {
            close(connfd);
            return PARSE_EXIT;
        }

        // Handle command and send ACK
        if (handle_cmd(buf) == RETURN_OK)
        {
            if (!send_ack(connfd, "RETURN_OK")) return PARSE_ERROR;
        }
        else
        {
            if (!send_ack(connfd, "RETURN_ERROR")) return PARSE_ERROR;
        }
    }

    return PARSE_SUCCESS;
}

static void handle_conn(int sockfd)
{
    int connfd;
    socklen_t len;
    struct sockaddr_in client;
    int ret;

    len = sizeof(client);

    while (true)
    {
        connfd = accept(sockfd, (struct sockaddr *)&client, &len);
        if (connfd < 0)
        {
            LOGE("accept failed: %s\n", strerror(errno));
            break;
        }

        ret = parse_cmd(connfd);
        if (ret == PARSE_SUCCESS) continue;
        if (ret == PARSE_EXIT) break;
        if (ret == PARSE_ERROR)
        {
            LOGE("CMD parse error!\n");
            break;
        }
    }
}

static bool parse_ip_port(const char *input_buffer, char *ip, size_t ipsize, int *port)
{
    char *buf;
    char *handle;
    char *token;
    bool ret = false;

    buf = STRDUP(input_buffer);
    handle = buf;

    token = strsep(&buf, ":");
    if (!token) goto exit;

    strncpy(ip, token, ipsize);

    token = strsep(&buf, ":");
    if (!token) goto exit;

    *port = strtol(token, NULL, 10);
    if (errno == ERANGE || errno == EINVAL)
    {
        LOGE("cannot get port number: %s\n", strerror(errno));
        goto exit;
    }

    ret = true;
exit:
    FREE(handle);
    return ret;
}

static void print_usage()
{
    LOG("\nusage: bs_testd [-v] [-B] [-b <ip:port>] [-s <ip:port>]\n"
        "\n\t-v: \n\t\tEnable verbose mode\n"
        "\n\t-b: \n\t\tbind ip and port number on which 'bs_testd' listens for commands from 'bs_cmd'. Default: 0.0.0.0:%d (any)\n"
        "\n\t-B: \n\t\trun as a deamon in background\n"
        "\n\t-s: \n\t\tif set, all output is additionally sent over UDP socket to the specified ip:port. Default: 0.0.0.0:%d\n", DEFAULT_PORT,
        DEFAULT_OUTPUT_PORT);
    exit(0);
}

static bool handle_args(args_t *args, int argc, char **argv)
{
    int c;

    opterr = 0;

    while ((c = getopt (argc, argv, "Bvhs:b:")) != -1)
    {
        switch (c)
        {
            case 'B':
                args->daemon = true;
                break;
            case 'v':
                g_verbose = true;
                break;
            case 'b':
                if (!parse_ip_port(optarg, args->cmd_ip, sizeof(args->cmd_ip), &args->cmd_port))
                {
                    LOGE("cannot parse -b parameter: %s\n", optarg);
                    return false;
                }
                break;
            case 's':
                if (!parse_ip_port(optarg, args->output_ip, sizeof(args->output_ip), &args->output_port))
                {
                    LOGE("cannot parse -s parameter: %s\n", optarg);
                    return false;
                }
                args->output_to_socket = true;
                break;
            case 'h':
                print_usage();
                break;
            case '?':
                if (isprint (optopt))
                {
                    LOGE("unknown option `-%c'.\n", optopt);
                    print_usage();
                }
                else
                {
                    LOGE("unknown option character `\\x%x'.\n", optopt);
                    print_usage();
                }
                return false;
            default:
                return false;
        }
    }

    return true;
}

int main(int argc, char **argv)
{
    int input_sockfd;
    int output_sockfd;
    args_t args;

    LOG("OpenSync Band Steering Test Daemon - awaiting commands\n");

    strncpy(args.cmd_ip, DEFAULT_IP, sizeof(args.cmd_ip));
    args.cmd_port = DEFAULT_PORT;
    args.daemon = false;
    args.output_to_socket = false;
    strncpy(args.output_ip, DEFAULT_IP, sizeof(args.output_ip));
    args.output_port = DEFAULT_OUTPUT_PORT;

    if (!handle_args(&args, argc, argv))
    {
        LOGE("cannot handle arguments\n");
        return 1;
    }

    if (args.daemon)
    {
        if (daemon(0, 1))
        {
            LOGE("daemon: %s\n", strerror(errno));
            return 1;
        }
    }

    if (args.output_to_socket)
    {
        output_sockfd = setup_output_socket(args.output_ip, args.output_port);
        if (output_sockfd == -1)
        {
            LOGE("cannot setup output socket\n");
            return 1;
        }
        g_output_sockfd = output_sockfd;
        LOGI("output socket is now active\n");
    }


    input_sockfd = setup_input_socket(args.cmd_ip, args.cmd_port);
    if (input_sockfd == -1)
    {
        LOGE("cannot setup input socket\n");
        return 1;
    }

    handle_conn(input_sockfd);

    LOGD("closing input socket\n");
    close(input_sockfd);
    if (args.output_to_socket)
    {
        LOGD("closing output socket\n");
        close(input_sockfd);
    }

    return 0;
}
