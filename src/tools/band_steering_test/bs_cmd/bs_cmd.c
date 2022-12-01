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
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "memutil.h"

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 5559
#define LOG_PREFIX(prefix, ...) printf("bs_cmd " prefix  __VA_ARGS__)
#define LOG(...) LOG_PREFIX("", __VA_ARGS__)
#define LOGE(...) LOG_PREFIX("[ERROR]: ", __VA_ARGS__)
#define LOGD(...) do { if (g_verbose) LOG_PREFIX("[DEBUG]: ", __VA_ARGS__); } while (0)
#define LOGI(...) LOG_PREFIX("[INFO]: ", __VA_ARGS__)

static bool g_verbose;

typedef struct
{
    char ip[128];
    int  port;
    char cmd[1024];
} args_t;

static int setup_socket(const char *ip, int port)
{
    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        LOGE("socket creation failed: %s\n", strerror(errno));
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0)
    {
        LOGE("connection with the server failed: %s\n", strerror(errno));
        return -1;
    }

    return sockfd;
}

static void send_cmd(int sockfd, const char *cmd)
{
    char buf[1024];

    memset(buf, 0, sizeof(buf));
    strncpy(buf, cmd, sizeof(buf));

    LOGI("sending >>%s<<\n", buf);
    write(sockfd, buf, strlen(buf));

    if (!strcmp(cmd, "exit")) return;

    memset(buf, 0, sizeof(buf));
    read(sockfd, buf, sizeof(buf));
    LOGI("return status: %s\n", buf);
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
    printf("\nusage: bs_cmd [-v] [-s <ip:port>] COMMAND\n"
           "\n\t-v: \n\t\tEnable verbose mode\n"
           "\n\t-s <ip:port>: \n\t\tIp and port number of server (bs_testd) to which commands will be sent. Default: %s:%d\n"
           "\n\tCOMMAND: \n\t\tOne of available commands (see below)\n\n", DEFAULT_IP, DEFAULT_PORT);
    printf("COMMANDS:\n");
    printf("\texit\n\t\tStop running 'bs_testd' instance\n");
    printf("\n\twifi_steering_eventRegister\n\t\tRegister debug callback that prints all the events\n");
    printf("\n\twifi_steering_eventUnregister\n"
           "\t\tUnregister debug callback\n");
#ifndef WIFI_HAL_VERSION_3_PHASE2
    printf("\n\twifi_steering_setGroup <GROUP_INDEX> <CFG_2_FILE || NULL> <CFG_5_FILE || NULL>\n"
           "\t\tCFG_2_FILE, CFG_5_FILE - paths to files that contain wifi_steering_apConfig_t configuration "
           "for cfg_2 and cfg_5 parameters\n");
#else
    printf("\n\twifi_steering_setGroup <GROUP_INDEX> <NUM_ELEMENTS> <CFG_FILE || NULL> ...\n"
           "\t\tCFG_FILE- path to file that contains wifi_steering_apConfig_t configuration"
#endif
    printf("\n\twifi_steering_clientDisconnect <GROUP_INDEX> <AP_INDEX> <MAC> <TYPE> <REASON>\n"
           "\t\tTYPE - 0: unknown, 1: disassoc, 2: deauth\n");
    printf("\n\twifi_steering_clientMeasure <GROUP_INDEX> <AP_INDEX> <MAC>\n"
           "\t\tIssue a request to perform SNR measurement\n");
    printf("\n\twifi_steering_clientSet <GROUP_INDEX> <AP_INDEX> <MAC> <CLIENT_CONFIG_FILE>\n"
           "\t\tCLIENT_CONFIG_FILE - path to file that contains per-client configuration (which include LWM/HWM settings)\n");
    printf("\n\twifi_steering_clientRemove <GROUP_INDEX> <AP_INDEX> <MAC>\n"
           "\t\tRemove BS configuration for client\n");
    printf("\n\twifi_setBTMRequest <AP_INDEX> <PEER_MAC> <BTM_REQUEST_FILE>\n"
           "\t\tBTM_REQUEST_FILE - path to file that contains parameters of BTM Request (including list of candidates)\n");
    printf("\n\twifi_setRMBeaconRequest <AP_INDEX> <PEER_MAC> <DIAL_TOKEN> <RM_REQUEST_FILE>\n"
           "\t\tRM_REQUEST_FILE - path to file that contains parameters of RM Beacon Request, DIAL_TOKEN: INT number (by default 0)\n");
    printf("\n\tget_bssTransitionActivated <AP_INDEX>\n"
           "\t\tCheck if BTM capability is enabled on the AP\n");
    printf("\n\tget_nbrReportActivated <AP_INDEX>\n"
           "\t\tCheck if RRM capability is enabled on the AP\n");
    printf("\n\tbssTransitionActivated <AP_INDEX> <STATE>\n"
           "\t\tSTATE - '0' disable BTM, '1' enable BTM\n");
    printf("\n\tnbrReportActivated <AP_INDEX> <STATE>\n"
           "\t\tSTATE - '0' disable RRM, '1' enable RRM\n\n");
    exit(0);
}

static bool serialize_ap_cfg_params(const char *cfg_name, char **ptr, int *bytes_left, int *counter)
{
    char *buf;
    char line[1024];
    const char *token = NULL;
    FILE *f = NULL;

    memset(line, 0, sizeof(line));

    f = fopen(cfg_name, "rt");
    if (f == NULL)
    {
        LOGE("Cannot open %s\n", cfg_name);
        return false;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        buf = line;
        token = strsep(&buf, "=");

        if (!token) continue;

        buf[strlen(buf) - 1] = '\0';

        if (!strcmp(token, "apIndex")) LOGD("apIndex = >>%s<<\n", buf);
        else if (!strcmp(token, "utilCheckIntervalSec")) LOGD("utilCheckIntervalSec = >>%s<<\n", buf);
        else if (!strcmp(token, "utilAvgCount")) LOGD("utilAvgCount = >>%s<<\n", buf);
        else if (!strcmp(token, "inactCheckIntervalSec")) LOGD("inactCheckIntervalSec = >>%s<<\n", buf);
        else if (!strcmp(token, "inactCheckThresholdSec")) LOGD("inactCheckThresholdSec = >>%s<<\n", buf);
        else
        {
            LOGE("Wrong token >>%s<<\n", token);
            fclose(f);
            return false;
        }

        *counter = snprintf(*ptr, *bytes_left, ";%s", buf);
        *bytes_left -= *counter;
        *ptr += *counter;
    }

    fclose(f);
    return true;
}

static bool serialize_cfg(const char *cfg_name, char **ptr, int *bytes_left, int *counter)
{
    if (strcmp(cfg_name, "NULL"))
    {
        if (!serialize_ap_cfg_params(cfg_name, ptr, bytes_left, counter)) return false;
    }
    else
    {
        *counter = snprintf(*ptr, *bytes_left, ";NULL");
        *bytes_left -= *counter;
        *ptr += *counter;
    }

    return true;
}

static bool handle_wifi_steering_setGroup(const char *name, const char *group_index_str, char **cfg_str,
                int cfg_str_number, char *cmd, size_t cmd_max_size)
{
    int bytes_left = cmd_max_size - 1;
    int counter = 0;
    char *ptr = cmd;
    int i;

    LOGD("group_index = %s\n", group_index_str);

    counter = snprintf(ptr, bytes_left, "%s;%s", name, group_index_str);
    bytes_left -= counter;
    ptr += counter;

    for (i = 0; i < cfg_str_number; i++)
    {
        if (!serialize_cfg(cfg_str[i], &ptr, &bytes_left, &counter))
        {
            LOGE("unable to serialize cfg %s\n", cfg_str[i]);
            return false;
        }
    }

    return true;
}

static bool handle_wifi_setBTMRequest(const char *name, const char *ap_index_str, const char *peer_mac_str,
                const char *btm_params_file, char *cmd, size_t cmd_max_size)
{
    int bytes_left = cmd_max_size - 1;
    int counter = 0;
    char *ptr = cmd;
    FILE *f = NULL;
    char *buf = NULL;
    char line[1024];
    const char *token = NULL;

    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("peer_mac = %s\n", peer_mac_str);

    counter = snprintf(ptr, bytes_left, "%s;%s;%s", name, ap_index_str, peer_mac_str);
    bytes_left -= counter;
    ptr += counter;

    f = fopen(btm_params_file, "rt");
    if (f == NULL)
    {
        LOGE("Cannot open %s\n", btm_params_file);
        return false;
    }

    memset(line, 0, sizeof(line));

    while (fgets(line, sizeof(line), f) != NULL)
    {
        buf = line;
        token = strsep(&buf, "=");

        if (!token) continue;

        buf[strlen(buf) - 1] = '\0';

        if (!strcmp(token, "token")) LOGD("token = >>%s<<\n", buf);
        else if (!strcmp(token, "requestMode")) LOGD("requestMode = >>%s<<\n", buf);
        else if (!strcmp(token, "timer")) LOGD("timer = >>%s<<\n", buf);
        else if (!strcmp(token, "validityInterval")) LOGD("validityInterval = >>%s<<\n", buf);
        else if (!strcmp(token, "termDuration.tsf")) LOGD("termDuration.tsf = >>%s<<\n", buf);
        else if (!strcmp(token, "termDuration.duration")) LOGD("termDuration.duration = >>%s<<\n", buf);
        else if (!strcmp(token, "numCandidates")) LOGD("numCandidates = >>%s<<\n", buf);
        else if (!strcmp(token, "bssid")) LOGD("bssid = >>%s<<\n", buf);
        else if (!strcmp(token, "info")) LOGD("info = >>%s<<\n", buf);
        else if (!strcmp(token, "opClass")) LOGD("opClass = >>%s<<\n", buf);
        else if (!strcmp(token, "channel")) LOGD("channel = >>%s<<\n", buf);
        else if (!strcmp(token, "phyTable")) LOGD("phyTable = >>%s<<\n", buf);
        else if (!strcmp(token, "preference")) LOGD("preference = >>%s<<\n", buf);
        else
        {
            LOGE("Wrong token >>%s<<\n", token);
            fclose(f);
            return false;
        }

        counter = snprintf(ptr, bytes_left, ";%s", buf);
        bytes_left -= counter;
        ptr += counter;
    }

    fclose(f);
    return true;
}

static bool handle_wifi_setRMBeaconRequest(const char *name, const char *ap_index_str, const char *peer_mac_str,
                const char *dial_token_str, const char *rm_request_file, char *cmd, size_t cmd_max_size)
{
    int bytes_left = cmd_max_size - 1;
    int counter = 0;
    char *ptr = cmd;
    FILE *f = NULL;
    char *buf = NULL;
    char line[1024];
    const char *token = NULL;

    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("peer_mac = %s\n", peer_mac_str);
    LOGD("dial_token = %s\n", dial_token_str);

    counter = snprintf(ptr, bytes_left, "%s;%s;%s;%s", name, ap_index_str, peer_mac_str, dial_token_str);
    bytes_left -= counter;
    ptr += counter;

    f = fopen(rm_request_file, "rt");
    if (f == NULL)
    {
        LOGE("Cannot open %s\n", rm_request_file);
        return false;
    }

    memset(line, 0, sizeof(line));

    while (fgets(line, sizeof(line), f) != NULL)
    {
        buf = line;
        token = strsep(&buf, "=");

        if (!token) continue;

        buf[strlen(buf) - 1] = '\0';

        if (!strcmp(token, "opClass")) LOGD("opClass = >>%s<<\n", buf);
        else if (!strcmp(token, "mode")) LOGD("mode = >>%s<<\n", buf);
        else if (!strcmp(token, "channel")) LOGD("channel = >>%s<<\n", buf);
        else if (!strcmp(token, "randomizationInterval")) LOGD("randomizationInterval = >>%s<<\n", buf);
        else if (!strcmp(token, "duration")) LOGD("duration = >>%s<<\n", buf);
        else if (!strcmp(token, "ssidPresent")) LOGD("ssidPresent = >>%s<<\n", buf);
        else if (!strcmp(token, "bssid")) LOGD("bssid = >>%s<<\n", buf);
        else
        {
            LOGE("Wrong token >>%s<<\n", token);
            fclose(f);
            return false;
        }

        counter = snprintf(ptr, bytes_left, ";%s", buf);
        bytes_left -= counter;
        ptr += counter;
    }

    fclose(f);
    return true;
}

static bool handle_wifi_steering_clientSet(const char *name, const char *group_index_str, const char *ap_index_str,
                const char *mac_str, const char *client_config_file, char *cmd, size_t cmd_max_size)
{
    int bytes_left = cmd_max_size - 1;
    int counter = 0;
    char *ptr = cmd;
    FILE *f = NULL;
    char *buf = NULL;
    char line[1024];
    const char *token = NULL;

    LOGD("group_index = %s\n", group_index_str);
    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("mac = %s\n", mac_str);

    counter = snprintf(ptr, bytes_left, "%s;%s;%s;%s", name, group_index_str, ap_index_str, mac_str);
    bytes_left -= counter;
    ptr += counter;

    f = fopen(client_config_file, "rt");
    if (f == NULL)
    {
        LOGE("Cannot open %s\n", client_config_file);
        return false;
    }

    memset(line, 0, sizeof(line));

    while (fgets(line, sizeof(line), f) != NULL)
    {
        buf = line;
        token = strsep(&buf, "=");

        if (!token) continue;

        buf[strlen(buf) - 1] = '\0';

        if (!strcmp(token, "rssiProbeHWM")) LOGD("rssiProbeHWM = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiProbeLWM")) LOGD("rssiProbeLWM = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiAuthHWM")) LOGD("rssiAuthHWM = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiAuthLWM")) LOGD("rssiAuthLWM = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiInactXing")) LOGD("rssiInactXing = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiHighXing")) LOGD("rssiHighXing = >>%s<<\n", buf);
        else if (!strcmp(token, "rssiLowXing")) LOGD("rssiLowXing = >>%s<<\n", buf);
        else if (!strcmp(token, "authRejectReason")) LOGD("authRejectReason = >>%s<<\n", buf);
        else
        {
            LOGE("Wrong token >>%s<<\n", token);
            fclose(f);
            return false;
        }

        counter = snprintf(ptr, bytes_left, ";%s", buf);
        bytes_left -= counter;
        ptr += counter;
    }

    fclose(f);
    return true;
}

static bool handle_wifi_steering_clientDisconnect(const char *name, const char *group_index_str, const char *ap_index_str,
                const char *mac_str, const char *type_str, const char *reason_str, char *cmd, size_t cmd_max_size)
{
    LOGD("group_index = %s\n", group_index_str);
    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("mac = %s\n", mac_str);
    LOGD("type_str = %s\n", type_str);
    LOGD("reason_str = %s\n", type_str);

    snprintf(cmd, cmd_max_size, "%s;%s;%s;%s;%s;%s", name, group_index_str, ap_index_str, mac_str, type_str, reason_str);

    return true;
}

static bool handle_wifi_steering_clientMeasure(const char *name, const char *group_index_str, const char *ap_index_str,
                const char *mac_str, char *cmd, size_t cmd_max_size)
{
    LOGD("group_index = %s\n", group_index_str);
    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("mac = %s\n", mac_str);

    snprintf(cmd, cmd_max_size, "%s;%s;%s;%s", name, group_index_str, ap_index_str, mac_str);

    return true;
}

static bool handle_wifi_steering_clientRemove(const char *name, const char *group_index_str, const char *ap_index_str,
                const char *mac_str, char *cmd, size_t cmd_max_size)
{
    LOGD("group_index = %s\n", group_index_str);
    LOGD("ap_index = %s\n", ap_index_str);
    LOGD("mac = %s\n", mac_str);

    snprintf(cmd, cmd_max_size, "%s;%s;%s;%s", name, group_index_str, ap_index_str, mac_str);

    return true;
}

static bool parse_cmd(int optind, char *cmd, size_t cmd_max_size, int argc, char **argv)
{
    int parameters = argc - optind;
    const unsigned int ap_cfg_offset = 2;

    LOGD("Have %d parameters to parse\n", parameters);

    if (parameters < 1)
    {
        print_usage();
        return false;
    }

    if (!strcmp(argv[optind], "wifi_steering_eventRegister"))
    {
        LOGI("wifi_steering_eventRegister\n");
        strncpy(cmd, "wifi_steering_eventRegister", cmd_max_size);
        return true;
    }

    if (!strcmp(argv[optind], "wifi_steering_eventUnregister"))
    {
        LOGI("wifi_steering_eventUnregister\n");
        strncpy(cmd, "wifi_steering_eventUnregister", cmd_max_size);
        return true;
    }

    if (!strcmp(argv[optind], "wifi_steering_setGroup"))
    {
        if (parameters < 4 || parameters > 20)
        {
            LOGE("Wrong number of parameters for wifi_steering_setGroup\n");
            print_usage();
            return false;
        }
        LOGI("wifi_steering_setGroup()\n");
        return handle_wifi_steering_setGroup(argv[optind], argv[optind + 1], &argv[optind + 2],
                parameters - ap_cfg_offset, cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_setBTMRequest"))
    {
        if (parameters != 4)
        {
            LOGE("Wrong number of parameters for wifi_setBTMRequest\n");
            print_usage();
            return false;
        }

        LOGI("wifi_setBTMRequest()\n");
        return handle_wifi_setBTMRequest(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_setRMBeaconRequest"))
    {
        if (parameters != 5)
        {
            LOGE("Wrong number of parameters for wifi_setRMBeaconRequest\n");
            print_usage();
            return false;
        }

        LOGI("wifi_setRMBeaconRequest()\n");
        return handle_wifi_setRMBeaconRequest(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], argv[optind + 4], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_steering_clientSet"))
    {
        if (parameters != 5)
        {
            LOGE("Wrong number of parameters for wifi_steering_clientSet\n");
            print_usage();
            return false;
        }

        LOGI("wifi_steering_clientSet()\n");
        return handle_wifi_steering_clientSet(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], argv[optind + 4], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_steering_clientDisconnect"))
    {
        if (parameters != 6)
        {
            LOGE("Wrong number of parameters for wifi_steering_clientDisconnect\n");
            print_usage();
            return false;
        }

        LOGI("wifi_steering_clientDisconnect()\n");
        return handle_wifi_steering_clientDisconnect(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], argv[optind + 4], argv[optind + 5], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_steering_clientMeasure"))
    {
        if (parameters != 4)
        {
            LOGE("Wrong number of parameters for wifi_steering_clientMeasure\n");
            print_usage();
            return false;
        }

        LOGI("wifi_steering_clientMeasure()\n");
        return handle_wifi_steering_clientMeasure(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_steering_clientRemove"))
    {
        if (parameters != 4)
        {
            LOGE("Wrong number of parameters for wifi_steering_clientRemove\n");
            print_usage();
            return false;
        }

        LOGI("wifi_steering_clientRemove()\n");
        return handle_wifi_steering_clientRemove(argv[optind], argv[optind + 1], argv[optind + 2],
                argv[optind + 3], cmd, cmd_max_size);
    }

    if (!strcmp(argv[optind], "wifi_steering_eventRegister"))
    {
        LOGI("wifi_steering_eventRegister()\n");
        snprintf(cmd, cmd_max_size, "wifi_steering_eventRegister");
        return true;
    }

    if (!strcmp(argv[optind], "wifi_steering_eventUnregister"))
    {
        LOGI("wifi_steering_eventUnregister()\n");
        snprintf(cmd, cmd_max_size, "wifi_steering_eventUnregister");
        return true;
    }

    if (!strcmp(argv[optind], "get_bssTransitionActivated"))
    {
        if (parameters != 2)
        {
            LOGE("Wrong number of parameters for get_bssTransitionActivated\n");
            print_usage();
            return false;
        }

        LOGI("get_bssTransitionActivated\n");
        snprintf(cmd, cmd_max_size, "get_bssTransitionActivated;%s", argv[optind + 1]);
        return true;
    }

    if (!strcmp(argv[optind], "get_nbrReportActivated"))
    {
        if (parameters != 2)
        {
            LOGE("Wrong number of parameters for get_nbrReportActivated\n");
            print_usage();
            return false;
        }

        LOGI("get_nbrReportActivated\n");
        snprintf(cmd, cmd_max_size, "get_nbrReportActivated;%s", argv[optind + 1]);
        return true;
    }

    if (!strcmp(argv[optind], "bssTransitionActivated"))
    {
        if (parameters != 3)
        {
            LOGE("Wrong number of parameters for bssTransitionActivated\n");
            print_usage();
            return false;
        }

        LOGI("bssTransitionActivated\n");
        snprintf(cmd, cmd_max_size, "bssTransitionActivated;%s;%s", argv[optind + 1],
            argv[optind + 2]);
        return true;
    }

    if (!strcmp(argv[optind], "nbrReportActivated"))
    {
        if (parameters != 3)
        {
            LOGE("Wrong number of parameters for nbrReportActivated\n");
            print_usage();
            return false;
        }

        LOGI("nbrReportActivated\n");
        snprintf(cmd, cmd_max_size, "nbrReportActivated;%s;%s", argv[optind + 1],
            argv[optind + 2]);
        return true;
    }

    if (!strcmp(argv[optind], "exit"))
    {
        LOGI("exit\n");
        snprintf(cmd, cmd_max_size, "exit");
        return true;
    }

    return false;
}

static bool handle_args(args_t *args, int argc, char **argv)
{
    int c;

    opterr = 0;

    while ((c = getopt (argc, argv, "hvs:")) != -1)
    {
        switch (c)
        {
            case 's':
                if (!parse_ip_port(optarg, args->ip, sizeof(args->ip), &args->port))
                {
                    LOGE("cannot parse -s parameter: %s\n", optarg);
                    return false;
                }
                break;
            case 'v':
                g_verbose = true;
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

    return parse_cmd(optind, args->cmd, sizeof(args->cmd), argc, argv);
}

int main(int argc, char **argv)
{
    int sockfd;
    args_t args;

    printf("Band Steering Test Command Utility\n");

    strncpy(args.ip, DEFAULT_IP, sizeof(args.ip));
    args.port = DEFAULT_PORT;
    memset(args.cmd, 0, sizeof(args.cmd));

    if (!handle_args(&args, argc, argv))
    {
        LOGE("cannot handle arguments\n");
        return 1;
    }

    LOGD("cmd = >>%s<<\n", args.cmd);

    sockfd = setup_socket(args.ip, args.port);
    if (sockfd == -1)
    {
        LOGE("cannot setup socket\n");
        return 1;
    }

    send_cmd(sockfd, args.cmd);
    close(sockfd);

    return 0;
}
