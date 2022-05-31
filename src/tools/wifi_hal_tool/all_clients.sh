#!/bin/sh

# Copyright (c) 2017, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


source "./common.sh"

NUMBER_OF_VAPS=$(get_key "output" "wifi_getSSIDNumberOfEntries")

for i in $(eval echo "{1..$NUMBER_OF_VAPS}")
do
    INDEX=$(($i - 1))
    OUTPUT=$(get_output wifi_getApAssociatedDeviceDiagnosticResult3 $INDEX)
    RET=$(get_key_raw "$OUTPUT" ret)
    CLIENT_NUM=$(get_key_raw "$OUTPUT" client_num)
    if [ "$RET" -ne "0" ];
    then
        continue
    fi
    if [ "$CLIENT_NUM" -eq "0" ];
    then
        continue
    fi

    echo "VAP index $INDEX:"
    echo "Client num: $CLIENT_NUM"

    SAVEIFS=$IFS
    IFS=$'\n'
    CLIENTS=($OUTPUT)
    IFS=$SAVEIFS

    for k in $(eval echo "{1..$CLIENT_NUM}")
    do
        CLIENT=${CLIENTS[$k]}
        MAC=$(get_key_raw "$CLIENT" cli_MACAddress)
        STATS=$(get_output wifi_getApAssociatedDeviceStats $INDEX $MAC)
        ACTIVE=$(get_key_raw "$CLIENT" cli_Active)
        SNR=$(get_key_raw "$CLIENT" cli_SNR)
        RSSI=$(get_key_raw "$CLIENT" cli_RSSI)
        STANDARD=$(get_key_string_raw "$CLIENT" cli_OperatingStandard)
        SENT=$(get_key_raw "$CLIENT" cli_BytesSent)
        RECEIVED=$(get_key_raw "$CLIENT" cli_BytesReceived)
        TXRATE=$(get_key_raw "$STATS" cli_tx_rate)
        RXRATE=$(get_key_raw "$STATS" cli_rx_rate)
        TXFRAMES=$(get_key_raw "$STATS" cli_tx_frames)
        RXFRAMES=$(get_key_raw "$STATS" cli_rx_frames)
        TXRETRIES=$(get_key_raw "$STATS" cli_tx_retries)
        RXRETRIES=$(get_key_raw "$STATS" cli_rx_retries)
        TXERRORS=$(get_key_raw "$STATS" cli_tx_errors)
        RXERRORS=$(get_key_raw "$STATS" cli_rx_errors)
        echo "#$k:"
        echo "    MAC: $MAC"
        echo "    Active: $ACTIVE"
        echo "    SNR: $SNR"
        echo "    RSSI: $RSSI"
        echo "    Standard: $STANDARD"
        echo "    Bytes sent: $SENT"
        echo "    Bytes received: $RECEIVED"
        echo "    TX rate: $TXRATE"
        echo "    RX rate: $RXRATE"
        echo "    TX frames: $TXFRAMES"
        echo "    RX frames: $RXFRAMES"
        echo "    TX retries: $TXRETRIES"
        echo "    RX retries: $RXRETRIES"
        echo "    TX errors: $TXERRORS"
        echo "    RX errors: $RXERRORS"
    done
done
