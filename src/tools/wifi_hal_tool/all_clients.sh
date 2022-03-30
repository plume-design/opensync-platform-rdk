#!/bin/sh

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
