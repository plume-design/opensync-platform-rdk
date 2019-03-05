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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

#include <ccsp/wifi_hal.h>

/*****************************************************************************/

#ifdef BCM_WIFI
extern INT wifi_context_init(void);
extern INT wifi_context_delete(void);
#endif /* BCM_WIFI */

#define WIFI_HAL_VER (WIFI_HAL_MAJOR_VERSION * 100 + WIFI_HAL_MINOR_VERSION * 10 + WIFI_HAL_MAINTENANCE_VERSION)
#if WIFI_HAL_VER >= 250
#define HAVE_wifi_getNeighboringWiFiStatus
#endif

#define AP_NAME_LEN 16

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#define LOG(LEVEL,FMT,...) printf("[%s] " FMT "\n", #LEVEL, ##__VA_ARGS__)

/*****************************************************************************/

bool opt_compact = true;
wifi_neighborScanMode_t opt_scan_mode = WIFI_RADIO_SCAN_MODE_FULL;
int opt_dwell = 10;
int opt_wait = 0;

bool print_compact = false;

/*****************************************************************************/

#define PRINT_FMT(NAME, TAG, FMT, VALUE) \
    do {\
        if (print_compact) { \
            char tmp[256]; \
            snprintf(tmp, sizeof(tmp), FMT, VALUE); \
            printf("%-4s ", tmp); \
        } else { \
            printf("  %-30s %s = ", NAME, TAG); \
            printf(FMT, VALUE); \
            printf("\n"); \
        } \
    } while (0)

#define PRINT_STR(A, B) PRINT_FMT(#B, "(str)", "'%s'", A->B)

#define PRINT_INT2(N, V) \
    do { \
        if (sizeof(V) == sizeof(int64_t)) { \
            if ( (typeof(V))-1 < 1 ) { \
                PRINT_FMT(N, "(i64)", "%"PRId64, (int64_t)V); \
            } else { \
                PRINT_FMT(N, "(u64)", "%"PRIu64, (uint64_t)V); \
            } \
        } else { \
            if ( (typeof(V))-1 < 1 ) { \
                PRINT_FMT(N, "(i32)", "%"PRId32, (int32_t)V); \
            } else { \
                PRINT_FMT(N, "(u32)", "%"PRIu32, (uint32_t)V); \
            } \
        } \
        fflush(stdout); \
    } while (0)

#define PRINT_INT1(V) PRINT_INT2(#V, V)

#define PRINT_INT(A, B) PRINT_INT2(#B, A->B)

#define PRINT_HEX2(N, V) \
    do { \
        if (sizeof(V) == sizeof(int64_t)) { \
            PRINT_FMT(N, "(x64)", "0x%"PRIx64, (uint64_t)V); \
        } else { \
            PRINT_FMT(N, "(x32)", "0x%"PRIx32, (uint32_t)V); \
        } \
    } while (0)

#define PRINT_HEX1(V) PRINT_HEX2(#V, V)

#define PRINT_HEX(A, B) PRINT_HEX2(#B, A->B)

#define PRINT_DOUBLE(A, B) PRINT_FMT(#B, "(dbl)", "%.2f", A->B);

/*****************************************************************************/

int64_t tv_to_ms(struct timeval tv)
{
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int64_t tv_delta_ms(struct timeval tv1, struct timeval tv2)
{
    return tv_to_ms(tv2) - tv_to_ms(tv1);
}

static char* _wifi_getRadioName(int index)
{
    static char name[AP_NAME_LEN];
    if (wifi_getRadioIfName(index, name) == RETURN_OK) return name;
    return NULL;
}

static char* _wifi_getApName(int index)
{
    static char name[AP_NAME_LEN];
    if (wifi_getApName(index, name) == RETURN_OK) return name;
    return NULL;
}

static char* fmt_mac_address_str(mac_address_t *mac, char *macstr, int size)
{
    snprintf(macstr, size, "%02X:%02X:%02X:%02X:%02X:%02X",
            (*mac)[0], (*mac)[1], (*mac)[2], (*mac)[3], (*mac)[4], (*mac)[5]);
    return macstr;
}

void parse_mac(char *macstr, mac_address_t *macp)
{
    unsigned char *mac = &(*macp)[0];
    int ret = sscanf(macstr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (ret != 6) {
        printf("sscanf()=%d of mac address failed", ret);
        exit(1);
    }
}

/*****************************************************************************/

void test_getRadioTrafficStats2(int index)
{
    wifi_radioTrafficStats2_t stats, *s = &stats;
    INT ret = 0;

    memset(&stats, 0, sizeof(stats));
    printf("test_getRadioTrafficStats2(%d)\n", index);
    ret = wifi_getRadioTrafficStats2(index, &stats); //Tr181
    printf("return: %d\n", ret);
    if (ret < 0) return;
    PRINT_INT(s, radio_BytesSent);
    PRINT_INT(s, radio_BytesReceived);
    PRINT_INT(s, radio_PacketsSent);
    PRINT_INT(s, radio_PacketsReceived);
    PRINT_INT(s, radio_ErrorsSent);
    PRINT_INT(s, radio_ErrorsReceived);
    PRINT_INT(s, radio_DiscardPacketsSent);
    PRINT_INT(s, radio_DiscardPacketsReceived);
    PRINT_INT(s, radio_PLCPErrorCount);
    PRINT_INT(s, radio_FCSErrorCount);
    PRINT_INT(s, radio_InvalidMACCount);
    PRINT_INT(s, radio_PacketsOtherReceived);
    PRINT_INT(s, radio_NoiseFloor);
    PRINT_INT(s, radio_ChannelUtilization);
    PRINT_INT(s, radio_ActivityFactor);
    PRINT_INT(s, radio_CarrierSenseThreshold_Exceeded);
    PRINT_INT(s, radio_RetransmissionMetirc);
    PRINT_INT(s, radio_MaximumNoiseFloorOnChannel);
    PRINT_INT(s, radio_MinimumNoiseFloorOnChannel);
    PRINT_INT(s, radio_MedianNoiseFloorOnChannel);
    PRINT_INT(s, radio_StatisticsStartTime);
}

void test_getSSIDTrafficStats2(int index)
{
    wifi_ssidTrafficStats2_t stats, *s = &stats;
    INT ret = 0;

    memset(&stats, 0, sizeof(stats));
    printf("wifi_getSSIDTrafficStats2(%d)\n", index);
    ret = wifi_getSSIDTrafficStats2(index, &stats); //Tr181
    printf("return: %d\n", ret);
    if (ret < 0) return;
    PRINT_INT(s, ssid_BytesSent);
    PRINT_INT(s, ssid_BytesReceived);
    PRINT_INT(s, ssid_PacketsSent);
    PRINT_INT(s, ssid_PacketsReceived);
    PRINT_INT(s, ssid_RetransCount);
    PRINT_INT(s, ssid_FailedRetransCount);
    PRINT_INT(s, ssid_RetryCount);
    PRINT_INT(s, ssid_MultipleRetryCount);
    PRINT_INT(s, ssid_ACKFailureCount);
    PRINT_INT(s, ssid_AggregatedPacketCount);
    PRINT_INT(s, ssid_ErrorsSent);
    PRINT_INT(s, ssid_ErrorsReceived);
    PRINT_INT(s, ssid_UnicastPacketsSent);
    PRINT_INT(s, ssid_UnicastPacketsReceived);
    PRINT_INT(s, ssid_DiscardedPacketsSent);
    PRINT_INT(s, ssid_DiscardedPacketsReceived);
    PRINT_INT(s, ssid_MulticastPacketsSent);
    PRINT_INT(s, ssid_MulticastPacketsReceived);
    PRINT_INT(s, ssid_BroadcastPacketsSent);
    PRINT_INT(s, ssid_BroadcastPacketsRecevied);
    PRINT_INT(s, ssid_UnknownPacketsReceived);
}

INT wait_neighbor_scan(bool use_new, INT apIndex,
        wifi_neighbor_ap2_t **neighbor_ap_array, UINT *output_array_size)
{
    INT rc;
    int timeout = 60;
    int t = 0;
    char *ifName = _wifi_getApName(apIndex);
    int radioIndex;

    do {
        errno = 0;
#ifdef HAVE_wifi_getNeighboringWiFiStatus
        if (use_new) {
            rc = wifi_getNeighboringWiFiStatus(apIndex, neighbor_ap_array, output_array_size);
            printf("wifi_getNeighboringWiFiStatus(apIndex:%d, size:%d) = %d errno=%d\n",
                    apIndex, *output_array_size, rc, errno);
        }
        else
#endif
        {
            rc = wifi_getSSIDRadioIndex(apIndex, &radioIndex);
            if (rc != RETURN_OK) {
                printf("wifi_getSSIDRadioIndex(%d) = %d\n", apIndex, rc);
                break;
            }
            rc = wifi_getNeighboringWiFiDiagnosticResult2(radioIndex, neighbor_ap_array, output_array_size);
            printf("wifi_getNeighboringWiFiDiagnosticResult2(radioIndex:%d, size:%d) = %d errno=%d\n",
                    radioIndex, *output_array_size, rc, errno);
        }
        if (rc != RETURN_OK && errno == EAGAIN) {
            LOG(INFO, "%d/%d Scan in progress... %s %d %s", t, timeout, ifName, errno, strerror(errno));
            sleep(1);
            t++;
        } else {
            break;
        }
    } while (t < timeout);
    if (rc != RETURN_OK) {
        LOG(ERR, "Failed to fetch %s neighbors %d %s", ifName, errno, strerror(errno));
    }
    return rc;
}

char* scan_mode_to_str(wifi_neighborScanMode_t scan_mode)
{
    switch (scan_mode) {
        case WIFI_RADIO_SCAN_MODE_FULL: return "full";
        case WIFI_RADIO_SCAN_MODE_ONCHAN: return "onchan";
        case WIFI_RADIO_SCAN_MODE_OFFCHAN: return "offchan";
        default: return "unknown";
    }
}

bool scan_mode_from_str(char *mode, wifi_neighborScanMode_t *scan_mode)
{
    if (!strcmp(mode, "full")) {
        *scan_mode = WIFI_RADIO_SCAN_MODE_FULL;
        return true;
    } else if (!strcmp(mode, "on") || !strcmp(mode, "onchan")) {
        *scan_mode = WIFI_RADIO_SCAN_MODE_ONCHAN;
        return true;
    } else if (!strcmp(mode, "off") || !strcmp(mode, "offchan")) {
        *scan_mode = WIFI_RADIO_SCAN_MODE_OFFCHAN;
        return true;
    }
    return false;
}

void run_startNeighborScan(int apIndex, wifi_neighborScanMode_t scan_mode, INT dwell, UINT num_ch, UINT *ch_list)
{
    int i = 0;
    INT ret = 0;
    struct timeval tv1, tv2;

    printf("wifi_startNeighborScan(apIndex:%d scan_mode:%s dwell:%d chan_num:%d",
            apIndex, scan_mode_to_str(scan_mode), dwell, num_ch);
    printf(" chan_list: [");
    for (i=0; i<(int)num_ch; i++) printf(" %d", ch_list[i]);
    printf(" ])\n");

    gettimeofday(&tv1, NULL);
    if (num_ch == 0) ch_list = NULL;
    ret = wifi_startNeighborScan(apIndex, scan_mode, dwell, num_ch, ch_list);
    gettimeofday(&tv2, NULL);
    printf("(run time: %d ms) return: %d\n", (int)tv_delta_ms(tv1, tv2), ret);
}

void test_startNeighborScan(int apIndex, int argc, char *argv[])
{
    wifi_neighborScanMode_t scan_mode;
    int dwell;
    int num_ch;
    int i;

    if (argc < 2) {
        printf("missing arguments\n");
        exit(1);
    }
    if (!scan_mode_from_str(argv[0], &scan_mode)) {
        printf("unknown scan_mode: %s\n", argv[0]);
        exit(1);
    }
    dwell = atoi(argv[1]);
    argc -= 2;
    argv += 2;
    num_ch = argc;
    UINT ch_list[num_ch];
    for (i=0; i<num_ch; i++) {
        ch_list[i] = atoi(argv[i]);
    }
    run_startNeighborScan(apIndex, scan_mode, dwell, num_ch, ch_list);
}

void get_neighbor_results(bool use_new, int apIndex)
{
    wifi_neighbor_ap2_t *neighbor_ap_array=NULL, *p=NULL;
    UINT array_size = 0;
    int i = 0;
    INT ret = 0;
    struct timeval tv1, tv2;

    gettimeofday(&tv1, NULL);
    ret = wait_neighbor_scan(use_new, apIndex, &neighbor_ap_array, &array_size);
    gettimeofday(&tv2, NULL);
    printf("(run time: %d) return: %d array_size=%d\n", (int)tv_delta_ms(tv1, tv2), ret, array_size);
    if (ret < 0) return;
    for (i=0, p=neighbor_ap_array; i<(int)array_size; i++, p++) {
        if (!opt_compact) {
            printf("  neighbor %d:\n", i);
            PRINT_STR(p, ap_SSID);
            PRINT_STR(p, ap_BSSID);
            PRINT_STR(p, ap_Mode);
            PRINT_INT(p, ap_Channel);
            PRINT_INT(p, ap_Channel);
            PRINT_INT(p, ap_SignalStrength);
            PRINT_STR(p, ap_SecurityModeEnabled);
            PRINT_STR(p, ap_EncryptionMode);
            PRINT_STR(p, ap_SupportedStandards);
            PRINT_STR(p, ap_OperatingStandards);
            PRINT_STR(p, ap_OperatingChannelBandwidth);
            PRINT_STR(p, ap_SecurityModeEnabled);
            PRINT_INT(p, ap_BeaconPeriod);
            PRINT_INT(p, ap_Noise);
            PRINT_STR(p, ap_BasicDataTransferRates);
            PRINT_STR(p, ap_SupportedDataTransferRates);
            PRINT_INT(p, ap_DTIMPeriod);
            PRINT_INT(p, ap_ChannelUtilization);
        } else {
            printf("  neighbor %2d: ch %3d w %-14s sig %3d %s '%s'\n", i,
                    p->ap_Channel,
                    p->ap_OperatingChannelBandwidth,
                    p->ap_SignalStrength,
                    p->ap_BSSID,
                    p->ap_SSID);
        }
    }
    if (neighbor_ap_array) free(neighbor_ap_array);
}

bool radio_to_apindex(int radioIndex, int *apIndex)
{
    int num_ssid;
    ULONG n;
    INT r;
    int i;
    // find first apIndex of the radioIndex
    *apIndex = -1;
    wifi_getSSIDNumberOfEntries(&n);
    num_ssid = n;
    for (i = 0; i < num_ssid; i++) {
        r = -1;
        wifi_getSSIDRadioIndex(i, &r);
        if (r == radioIndex) {
            *apIndex = i;
            return true;
        }
    }
    printf("No apIndex found for radio %d\n", radioIndex);
    return false;
}

void test_getNeighboringWiFiDiagnosticResult2(int radioIndex)
{
    int apIndex;
    if (!radio_to_apindex(radioIndex, &apIndex)) exit(1);
    get_neighbor_results(false, apIndex);
}

void test_getNeighboringWiFiStatus(int apIndex)
{
    get_neighbor_results(true, apIndex);
}

void test_neighbors(bool use_new, int apIndex, int num_ch, char **ch_array)
{
    int i;
    if (num_ch < 0) num_ch = 0;
    UINT ch_list[num_ch];
    for (i=0; i<num_ch; i++) {
        ch_list[i] = atoi(ch_array[i]);
    }
    run_startNeighborScan(apIndex, opt_scan_mode, opt_dwell, num_ch, (num_ch > 0) ? ch_list : NULL);
    if (opt_wait) {
        printf("sleep %d ms\n", opt_wait);
        usleep(opt_wait * 1000);
    }
    get_neighbor_results(use_new, apIndex);
}


void test_getRadioChannelStats(int index)
{
#define NUM_CH_24 11 // 1-11
#define NUM_CH_5  33 // 36-165
#define NUM_CH_ALL (NUM_CH_24 + NUM_CH_5) // 1-165
    wifi_channelStats_t ch_stats[NUM_CH_ALL], *s;
    int i, n;
    INT ret = 0;

    memset(&ch_stats, 0, sizeof(ch_stats));
    for (n=0; n < NUM_CH_24; n++) {
        ch_stats[n].ch_number = 1 + n;
        ch_stats[n].ch_in_pool = true;
    }
    for (i=0; i < NUM_CH_5; i++,n++) {
        ch_stats[n].ch_number = 36 + i * 4;
        if (ch_stats[n].ch_number >= 148) {
            ch_stats[n].ch_number++;
        }
        ch_stats[n].ch_in_pool = true;
    }
    printf("wifi_getRadioChannelStats(%d size=%d)\n", index, ARRAY_SIZE(ch_stats));
    ret = wifi_getRadioChannelStats(index, ch_stats, ARRAY_SIZE(ch_stats));
    printf("return: %d\n", ret);
    if (ret < 0) return;
    n = 0;
    for (i=0; i<(int)ARRAY_SIZE(ch_stats); i++) {
        s = &ch_stats[i];
        if (!s->ch_in_pool) continue;
        if (   !s->ch_utilization_busy_tx
            && !s->ch_utilization_busy_rx
            && !s->ch_utilization_busy
            && !s->ch_utilization_total)
        {
            continue;
        }
        printf("  ch %3u tx %10"PRIu64" rx %10"PRIu64" busy %10"PRIu64" total %10"PRIu64"\n",
                s->ch_number,
                s->ch_utilization_busy_tx,
                s->ch_utilization_busy_rx,
                s->ch_utilization_busy,
                s->ch_utilization_total);
        n++;
    }
    printf("  channels with stats: %d\n", n);
}

void test_getApAssociatedDeviceRxStatsResult(int index, char *mac_str)
{
    mac_address_t mac;
    wifi_associated_dev_rate_info_rx_stats_t *stats_array, *s;
    UINT output_array_size;
    ULLONG handle;
    int i, j, k;
    INT ret = 0;

    parse_mac(mac_str, &mac);
    printf("wifi_getApAssociatedDeviceRxStatsResult(%d %s)\n", index, mac_str);
    ret = wifi_getApAssociatedDeviceRxStatsResult(index, &mac, &stats_array, &output_array_size, &handle);
    printf("return: %d output_array_size: %d\n", ret, output_array_size);
    PRINT_HEX1(handle);
    if (ret < 0) return;
    if (opt_compact) {
        print_compact = true;
        printf("  [i  bw   nss  mcs] flag byte msdu mpdu ppdu retr rssi\n");
    }
    for (i=0; i<(int)output_array_size; i++) {
        if (opt_compact) printf("   %-2d ", i);
        else printf("  [%d]\n", i);
        s = &stats_array[i];
        PRINT_INT(s, bw);
        PRINT_INT(s, nss);
        PRINT_INT(s, mcs);
        PRINT_HEX(s, flags);
        PRINT_INT(s, bytes);
        PRINT_INT(s, msdus);
        PRINT_INT(s, mpdus);
        PRINT_INT(s, ppdus);
        PRINT_INT(s, retries);
        PRINT_INT(s, rssi_combined);
        if (opt_compact) {
            printf("[");
            for (j=0; j<8; j++) {
                if (j) printf(",");
                for (k=0; k<4; k++) {
                    printf(" %2d", s->rssi_array[j][k]);
                }
            }
            printf(" ]\n");
        } else {
            printf("  rssi_array:\n");
            for (j=0; j<8; j++) {
                printf("     ");
                for (k=0; k<4; k++) {
                    printf("%-2d ", s->rssi_array[j][k]);
                }
                printf("\n");
            }
        }
    }
    print_compact = false;
    free(stats_array);
}

void test_getApAssociatedDeviceTxStatsResult(int index, char *mac_str)
{
    mac_address_t mac;
    wifi_associated_dev_rate_info_tx_stats_t *stats_array, *s;
    UINT output_array_size;
    ULLONG handle;
    int i;
    INT ret = 0;

    parse_mac(mac_str, &mac);
    printf("wifi_getApAssociatedDeviceTxStatsResult(%d %s)\n", index, mac_str);
    ret = wifi_getApAssociatedDeviceTxStatsResult(index, &mac, &stats_array, &output_array_size, &handle);
    printf("return: %d output_array_size: %d\n", ret, output_array_size);
    PRINT_HEX1(handle);
    if (ret < 0) return;
    if (opt_compact) {
        print_compact = true;
        printf("  [i  bw   nss  mcs] flag byte msdu mpdu ppdu retr attempts\n");
    }
    for (i=0; i<(int)output_array_size; i++) {
        if (opt_compact) printf("   %-2d ", i);
        else printf("  [%d]\n", i);
        s = &stats_array[i];
        PRINT_INT(s, bw);
        PRINT_INT(s, nss);
        PRINT_INT(s, mcs);
        PRINT_HEX(s, flags);
        PRINT_INT(s, bytes);
        PRINT_INT(s, msdus);
        PRINT_INT(s, mpdus);
        PRINT_INT(s, ppdus);
        PRINT_INT(s, retries);
        PRINT_INT(s, attempts);
        if (opt_compact) printf("\n");
    }
    print_compact = false;
    free(stats_array);
}

void test_getApAssociatedDeviceTidStatsResult(int index, char *mac_str)
{
    mac_address_t mac;
    wifi_associated_dev_tid_stats_t tid_stats;
    wifi_associated_dev_tid_entry_t *s;
    ULLONG handle;
    int i;
    INT ret = 0;

    parse_mac(mac_str, &mac);
    printf("wifi_getApAssociatedDeviceTidStatsResult(%d %s)\n", index, mac_str);
    ret = wifi_getApAssociatedDeviceTidStatsResult(index, &mac, &tid_stats, &handle);
    printf("return: %d\n", ret);
    PRINT_HEX1(handle);
    if (ret < 0) return;
    printf("  [i  ac  tid] ewma_time_ms sum_time_ms num_msdus\n");
    for (i=0; i<(int)ARRAY_SIZE(tid_stats.tid_array); i++) {
        s = &tid_stats.tid_array[i];
        printf("   %-2d %-3d %-3d  %-12"PRIu64" %-11"PRIu64" %"PRIu64"\n",
                i, s->ac, s->tid,
                s->ewma_time_ms,
                s->sum_time_ms,
                s->num_msdus);
    }
}

void test_getApAssociatedDeviceStats(int index, char *mac_str)
{
    mac_address_t mac;
    wifi_associated_dev_stats_t dev_stats, *s = &dev_stats;
    ULLONG handle;
    INT ret = 0;

    parse_mac(mac_str, &mac);
    printf("wifi_getApAssociatedDeviceStats(%d %s)\n", index, mac_str);
    ret = wifi_getApAssociatedDeviceStats(index, &mac, &dev_stats, &handle);
    printf("return: %d\n", ret);
    PRINT_HEX1(handle);
    PRINT_INT(s, cli_rx_bytes);
    PRINT_INT(s, cli_tx_bytes);
    PRINT_INT(s, cli_rx_frames);
    PRINT_INT(s, cli_tx_frames);
    PRINT_INT(s, cli_rx_retries);
    PRINT_INT(s, cli_tx_retries);
    PRINT_INT(s, cli_rx_errors);
    PRINT_INT(s, cli_tx_errors);
    PRINT_DOUBLE(s, cli_rx_rate);
    PRINT_DOUBLE(s, cli_tx_rate);
}

void print_wifi_associated_dev(wifi_associated_dev_t *p)
{
    char mac_str[32];
    fmt_mac_address_str(&p->cli_MACAddress, mac_str, sizeof(mac_str));
    PRINT_FMT("cli_MACAddress", "(mac)", "%s", mac_str);
    PRINT_STR(p, cli_IPAddress);
    PRINT_INT(p, cli_AuthenticationState);
    PRINT_INT(p, cli_LastDataDownlinkRate);
    PRINT_INT(p, cli_LastDataUplinkRate);
    PRINT_INT(p, cli_SignalStrength);
    PRINT_INT(p, cli_Retransmissions);
    PRINT_INT(p, cli_Active);
    PRINT_STR(p, cli_OperatingStandard);
    PRINT_STR(p, cli_OperatingChannelBandwidth);
    PRINT_INT(p, cli_SNR);
    PRINT_STR(p, cli_InterferenceSources);
    PRINT_INT(p, cli_DataFramesSentAck);
    PRINT_INT(p, cli_DataFramesSentNoAck);
    PRINT_INT(p, cli_BytesSent);
    PRINT_INT(p, cli_BytesReceived);
    PRINT_INT(p, cli_RSSI);
    PRINT_INT(p, cli_MinRSSI);
    PRINT_INT(p, cli_MaxRSSI);
    PRINT_INT(p, cli_Disassociations);
    PRINT_INT(p, cli_AuthenticationFailures);
}

void print_wifi_associated_dev2(wifi_associated_dev2_t *p)
{
    char mac_str[32];
    fmt_mac_address_str(&p->cli_MACAddress, mac_str, sizeof(mac_str));
    PRINT_FMT("cli_MACAddress", "(mac)", "%s", mac_str);
    PRINT_STR(p, cli_IPAddress);
    PRINT_INT(p, cli_AuthenticationState);
    PRINT_INT(p, cli_LastDataDownlinkRate);
    PRINT_INT(p, cli_LastDataUplinkRate);
    PRINT_INT(p, cli_SignalStrength);
    PRINT_INT(p, cli_Retransmissions);
    PRINT_INT(p, cli_Active);
    PRINT_STR(p, cli_OperatingStandard);
    PRINT_STR(p, cli_OperatingChannelBandwidth);
    PRINT_INT(p, cli_SNR);
    PRINT_STR(p, cli_InterferenceSources);
    PRINT_INT(p, cli_DataFramesSentAck);
    PRINT_INT(p, cli_DataFramesSentNoAck);
    PRINT_INT(p, cli_BytesSent);
    PRINT_INT(p, cli_BytesReceived);
    PRINT_INT(p, cli_RSSI);
    PRINT_INT(p, cli_MinRSSI);
    PRINT_INT(p, cli_MaxRSSI);
    PRINT_INT(p, cli_Disassociations);
    PRINT_INT(p, cli_AuthenticationFailures);
    PRINT_INT(p, cli_Associations);
}

void test_getApAssociatedDeviceDiagnosticResult2(int index, char *one_mac, void(*cb)(int i, int apIndex, char *mac_str))
{
    wifi_associated_dev2_t *associated_dev_array=NULL, *pt=NULL;
    UINT array_size=0;
    UINT i=0;
    INT ret = 0;
    char mac_str[32];

    printf("wifi_getApAssociatedDeviceDiagnosticResult2(%d)\n", index);
    ret = wifi_getApAssociatedDeviceDiagnosticResult2(index, &associated_dev_array, &array_size);
    printf("return: %d array_size: %d\n", ret, array_size);
    if (ret < 0) return;
    for (i=0, pt=associated_dev_array; i<array_size; i++, pt++) {
        fmt_mac_address_str(&pt->cli_MACAddress, mac_str, sizeof(mac_str));
        if (one_mac && strcasecmp(one_mac, mac_str)) continue;
        printf("client [%d] / %d:\n", i, array_size);
        print_wifi_associated_dev2(pt);
        if (cb) cb(i, index, mac_str);
    }
    if (associated_dev_array) {
        free(associated_dev_array); //make sure to free the list
    }
}

void print_radio_banner(int i, char *pband, ULONG *pchannel)
{
    char band[64];
    ULONG channel;
    if (!pband) pband = band;
    if (!pchannel) pchannel = &channel;

    wifi_getRadioOperatingFrequencyBand(i, pband);
    wifi_getRadioChannel(i, pchannel);
    printf("--- RADIO %d: %-8s band: %6s channel: %d\n",
            i, _wifi_getRadioName(i),
            pband, (int)*pchannel);
}

void all_radios()
{
    int i;
    ULONG nr;

    printf("===== RADIOS =====\n\n");

    wifi_getRadioNumberOfEntries(&nr);

    for (i=0; i < (int)nr; i++) {
        print_radio_banner(i, NULL, NULL);
        test_getRadioTrafficStats2(i);
        printf("\n");
        test_getRadioChannelStats(i);
        printf("\n");
    }
}

void print_ap_banner(int i)
{
    int r;
    char ssid[64];
    BOOL enabled;
    char status[64];
    wifi_getApRadioIndex(i, &r);
    *ssid=0;
    wifi_getSSIDName(i, ssid);
    wifi_getApEnable(i, &enabled);
    wifi_getApStatus(i, status);
    printf("--- RADIO: %d %-8s AP: %-2d IFNAME: %-8s STATUS: %d %-7s SSID: '%s'\n",
            r, _wifi_getRadioName(r),
            i, _wifi_getApName(i),
            enabled, status, ssid);
}

void all_aps()
{
    int i;
    ULONG n;

    printf("===== APS =====\n\n");

    wifi_getSSIDNumberOfEntries(&n);

    for (i=0; i < (int)n; i++) {
        print_ap_banner(i);
        test_getSSIDTrafficStats2(i);
        printf("\n");
    }
}

void all_neighbors(bool use_new)
{
    int i;
    int apIndex;
    ULONG nr;
    char band[64];
    ULONG channel;

    printf("===== NEIGHBORS =====\n\n");

    wifi_getRadioNumberOfEntries(&nr);

    for (i=0; i < (int)nr; i++) {
        print_radio_banner(i, band, &channel);
        if (!radio_to_apindex(i, &apIndex)) continue;
        int n_ch;
        char **ch_list;
        char ch_current[32];
        char *ch_on[1] = { ch_current };
        const char *ch_list_24[] = { "1", "6", "11" };
        const char *ch_list_5[] = { "40", "153" };
        if (opt_scan_mode == WIFI_RADIO_SCAN_MODE_ONCHAN) {
            snprintf(ch_current, sizeof(ch_current), "%d", (int)channel);
            ch_list = ch_on;
            n_ch = 1;
        } else {
            if (*band == '5') {
                ch_list = (char**)ch_list_5;
                n_ch = 2;
            } else {
                ch_list = (char**)ch_list_24;
                n_ch = 3;
            }
        }
        test_neighbors(use_new, apIndex, n_ch, ch_list);
        printf("\n");
    }
}

void print_client_details(int i, int apIndex, char *mac_str)
{
    int statsIndex;
#ifdef BCM_WIFI
    statsIndex = apIndex;
    printf("client %d: %s (ap:%d %s) details:\n",
            i, mac_str, apIndex, _wifi_getApName(apIndex));
#else  /* not BCM_WIFI */
    wifi_getSSIDRadioIndex(apIndex, &statsIndex);
    printf("client %d: %s (ap:%d %s radio:%d %s) details:\n",
            i, mac_str, apIndex, _wifi_getApName(apIndex),
            statsIndex, _wifi_getRadioName(statsIndex));
#endif /* not BCM_WIFI */
    test_getApAssociatedDeviceStats(apIndex, mac_str);
    test_getApAssociatedDeviceRxStatsResult(statsIndex, mac_str);
    test_getApAssociatedDeviceTxStatsResult(statsIndex, mac_str);
    test_getApAssociatedDeviceTidStatsResult(statsIndex, mac_str);
    printf("\n");
}

void all_clients()
{
    int i;
    ULONG n;

    printf("===== CLIENTS =====\n\n");

    wifi_getSSIDNumberOfEntries(&n);

    for (i=0; i < (int)n; i++) {
        print_ap_banner(i);
        test_getApAssociatedDeviceDiagnosticResult2(i, NULL, print_client_details);
        printf("\n");
    }
}

void all()
{
    all_radios();
    all_aps();
    all_neighbors(false);
    all_clients();
}

INT new_client_callback(INT apIndex, wifi_associated_dev_t *associated_dev)
{
    printf("NEW CLIENT: apIndex=%d\n", apIndex);
    print_wifi_associated_dev(associated_dev);
    printf("\n");
    return RETURN_OK;
}

void test_newApAssociatedDevice_callback()
{
    printf("NOT SUPPORTED\n");
}

void test_pushRadioChannel(int radioIndex, char *channel_str)
{
    UINT channel = atoi(channel_str);
    INT ret;
    struct timeval tv1, tv2;

    printf("wifi_pushRadioChannel(radioIndex:%d channel:%d)\n", radioIndex, channel);
    gettimeofday(&tv1, NULL);
    ret = wifi_pushRadioChannel(radioIndex, channel);
    gettimeofday(&tv2, NULL);
    printf("return: %d\n", ret);
    printf("time: %d ms\n", (int)tv_delta_ms(tv1, tv2));
}

void test_pushRadioChannel2(int radioIndex, char *channel_str, char *width_str, char *count_str)
{
    UINT channel = atoi(channel_str);
    UINT width = atoi(width_str);
    UINT count = atoi(count_str);
    INT ret;
    struct timeval tv1, tv2;

    printf("wifi_pushRadioChannel2(radioIndex:%d channel:%d width:%d count:%d)\n",
            radioIndex, channel, width, count);
    gettimeofday(&tv1, NULL);
    ret = wifi_pushRadioChannel2(radioIndex, channel, width, count);
    gettimeofday(&tv2, NULL);
    printf("return: %d\n", ret);
    printf("time: %d ms\n", (int)tv_delta_ms(tv1, tv2));
}

void test_size()
{
#define PRINT_SIZE(T) printf("sizeof(%s) = %d\n", #T, sizeof(T))
    PRINT_SIZE(unsigned char);
    PRINT_SIZE(unsigned int);
    PRINT_SIZE(unsigned long);
    PRINT_SIZE(unsigned long long);
    PRINT_SIZE(double);
    PRINT_SIZE(BOOL);
    PRINT_SIZE(UINT);
    PRINT_SIZE(ULONG);
    PRINT_SIZE(ULLONG);
#undef PRINT_SIZE
}


/*****************************************************************************/

void help()
{
    int i;
    ULONG nr, n;

    printf("Usage: wifi_hal [OPT] <CMD> <INDEX> [ARG]\n");
    printf("  OPT:\n");
    printf("    -h          : help\n");
    printf("    -c          : compact output\n");
    printf("    -v          : verbose output\n");
    printf("    -f          : off-chan neighbor scan\n");
    printf("    -n          : on-chan neighbor scan\n");
    printf("    -u          : full neighbor scan\n");
    printf("    -d DWELL    : neighbor scan dwell time\n");
    printf("    -w MILLISEC : neighbor wait time between scan and get\n");
    printf("  CMD:\n");
    printf("    all\n");
    printf("    all_radios\n");
    printf("    all_aps\n");
    printf("    all_neighbors\n");
    printf("    all_neighbors2\n");
    printf("    all_clients\n");
    printf("    getRadioTrafficStats2                  <RADIO INDEX>\n");
    printf("    getRadioChannelStats                   <RADIO INDEX>\n");
    printf("    getSSIDTrafficStats2                   <AP INDEX>\n");
    // neighbors
    printf("    neighbors                              <AP INDEX> [CHAN LIST]\n");
    printf("       == startNeighborScan + getNeighboringWiFiDiagnosticResult2\n");
#ifdef HAVE_wifi_getNeighboringWiFiStatus
    printf("    neighbors2                             <AP INDEX> [CHAN LIST]\n");
    printf("       == startNeighborScan + getNeighboringWiFiStatus\n");
#endif
    printf("    startNeighborScan                      <AP INDEX> <SCAN_MODE> <DWELL> [CHAN LIST]\n");
    printf("                                           SCAN_MODE: full, on, off\n");
    printf("    getNeighboringWiFiDiagnosticResult2    <RADIO INDEX>\n");
#ifdef HAVE_wifi_getNeighboringWiFiStatus
    printf("    getNeighboringWiFiStatus               <AP INDEX>\n");
#endif
    // clients
    printf("    client                                 <AP INDEX> <MAC>\n");
    printf("    getApAssociatedDeviceDiagnosticResult2 <AP INDEX>\n");
    printf("    getApAssociatedDeviceStats             <AP INDEX> <MAC>\n");
    printf("    getApAssociatedDeviceRxStatsResult     <RADIO INDEX> <MAC>\n");
    printf("    getApAssociatedDeviceTxStatsResult     <RADIO INDEX> <MAC>\n");
    printf("    getApAssociatedDeviceTidStatsResult    <RADIO INDEX> <MAC>\n");
    printf("    newApAssociatedDevice_callback\n");
    printf("    pushRadioChannel                       <RADIO INDEX> <CHANNEL>\n");
    printf("    pushRadioChannel2                      <RADIO INDEX> <CHANNEL> <WIDTH> <CSA_BEACON_COUNT>\n");

    printf("radio indexes:\n");
    wifi_getRadioNumberOfEntries(&nr);
    for (i=0; i < (int)nr; i++) {
        print_radio_banner(i, NULL, NULL);
    }

    printf("ap indexes:\n");
    wifi_getSSIDNumberOfEntries(&n);
    for (i=0; i < (int)n; i++) {
        print_ap_banner(i);
    }

    printf("WIFI_HAL HEADER  Version: %d.%d.%d\n",
            WIFI_HAL_MAJOR_VERSION,
            WIFI_HAL_MINOR_VERSION,
            WIFI_HAL_MAINTENANCE_VERSION);

    char halver[256] = "";
    wifi_getHalVersion(halver);
    printf("WIFI_HAL LIBRARY Version: %s\n", halver);

    exit(1);
}


/*****************************************************************************/

int main(int argc,char **argv)
{
    int index;
    int opt;
    char *cmd;
    char *arg2 = NULL;
    char **xargv = NULL;
    int xargc = 0;

#ifdef BCM_WIFI
    if (wifi_context_init() != RETURN_OK) {
        LOG(ERR, "BCM_WIFI wifi_context_init() failed!");
        return 0;
    }
#endif /* BCM_WIFI */

    do {
        opt = getopt(argc, argv, "hcvfnud:w:");
        switch (opt) {
            case 'h': help(); break;
            case 'c': opt_compact = true; break;
            case 'v': opt_compact = false; break;
            case 'f': opt_scan_mode = WIFI_RADIO_SCAN_MODE_OFFCHAN;
                      printf("neighbor scan mode: off-chan\n");
                      break;
            case 'n': opt_scan_mode = WIFI_RADIO_SCAN_MODE_ONCHAN;
                      printf("neighbor scan mode: on-chan\n");
                      break;
            case 'u': opt_scan_mode = WIFI_RADIO_SCAN_MODE_FULL;
                      printf("neighbor scan mode: full\n");
                      break;
            case 'd': opt_dwell = atoi(optarg);
                      printf("neighbor dwell time: %d\n", opt_dwell);
                      break;
            case 'w': opt_wait = atoi(optarg);
                      printf("neighbor wait time: %d\n", opt_wait);
                      break;
            case -1: break;
            default: help(); break;
        }
    } while (opt >= 0);

    // no arguments
    if (optind + 1 > argc) {
        help();
    }
    // 1 argument
    cmd = argv[optind];
    if (!strcmp(cmd, "help")) {
        help();
    }
    else if (!strcmp(cmd, "size")) {
        test_size();
        return 0;
    }
    else if (!strcmp(cmd, "all")) {
        all();
        return 0;
    }
    else if (!strcmp(cmd, "all_radios")) {
        all_radios();
        return 0;
    }
    else if (!strcmp(cmd, "all_aps")) {
        all_aps();
        return 0;
    }
    else if (!strcmp(cmd, "all_neighbors")) {
        all_neighbors(false);
        return 0;
    }
    else if (!strcmp(cmd, "all_neighbors2")) {
        all_neighbors(true);
        return 0;
    }
    else if (!strcmp(cmd, "all_clients")) {
        all_clients();
        return 0;
    }
    else if (!strcmp(cmd, "newApAssociatedDevice_callback")) {
        test_newApAssociatedDevice_callback();
        return 0;
    }
    if (optind + 2 > argc) {
        help();
    }
    // 2 arguments
    index = atoi(argv[optind + 1]);
    // 3 arguments or more
    if (optind + 3 <= argc) {
        arg2 = argv[optind + 2];
        xargv = &argv[optind + 2];
        xargc = argc - optind - 2;
    }
    // wifi_ optional
    if (strncmp(cmd, "wifi_", 5) == 0) {
        cmd += 5;
    }

    if (!strcmp(cmd, "getRadioTrafficStats2")) {
        test_getRadioTrafficStats2(index);
    }
    else if (!strcmp(cmd, "getRadioChannelStats")) {
        test_getRadioChannelStats(index);
    }
    else if (!strcmp(cmd, "getSSIDTrafficStats2")) {
        test_getSSIDTrafficStats2(index);
    }
    else if (!strcmp(cmd, "neighbors")) {
        test_neighbors(false, index, xargc, xargv);
    }
    else if (!strcmp(cmd, "neighbors2")) {
        test_neighbors(true, index, xargc, xargv);
    }
    else if (!strcmp(cmd, "startNeighborScan")) {
        test_startNeighborScan(index, xargc, xargv);
    }
    else if (!strcmp(cmd, "getNeighboringWiFiStatus")) {
        test_getNeighboringWiFiStatus(index);
    }
    else if (!strcmp(cmd, "getNeighboringWiFiDiagnosticResult2")) {
        test_getNeighboringWiFiDiagnosticResult2(index);
    }
    else if (!strcmp(cmd, "getApAssociatedDeviceDiagnosticResult2")) {
        test_getApAssociatedDeviceDiagnosticResult2(index, NULL, NULL);
    }
    else if (!strcmp(cmd, "client")) {
        if (!arg2) help();
        test_getApAssociatedDeviceDiagnosticResult2(index, arg2, print_client_details);
    }
    else if (!strcmp(cmd, "getApAssociatedDeviceStats")) {
        if (!arg2) help();
        test_getApAssociatedDeviceStats(index, arg2);
    }
    else if (!strcmp(cmd, "getApAssociatedDeviceRxStatsResult")) {
        if (!arg2) help();
        test_getApAssociatedDeviceRxStatsResult(index, arg2);
    }
    else if (!strcmp(cmd, "getApAssociatedDeviceTxStatsResult")) {
        if (!arg2) help();
        test_getApAssociatedDeviceTxStatsResult(index, arg2);
    }
    else if (!strcmp(cmd, "getApAssociatedDeviceTidStatsResult")) {
        if (!arg2) help();
        test_getApAssociatedDeviceTidStatsResult(index, arg2);
    }
    else if (!strcmp(cmd, "pushRadioChannel")) {
        if (!arg2) help();
        test_pushRadioChannel(index, arg2);
    }
    else if (!strcmp(cmd, "pushRadioChannel2")) {
        if (xargc < 2) help();
        test_pushRadioChannel2(index, arg2, xargv[1], xargv[2]);
    }
    else {
        help();
    }

#ifdef BCM_WIFI
    wifi_context_delete();
#endif /* BCM_WIFI */

    return 0;
}
