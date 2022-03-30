#!/bin/sh

source "./common.sh"

get_channels_map()
{
    CMD_OUTPUT="$($TOOL "wifi_getRadioChannels" "$1" | grep "$LOG_PREFIX" | cut -d " " -f 6-)"

    for i in $CMD_OUTPUT
    do
        CHANNEL="$(echo $i | awk -F':' '{print $1}' | tr -d "channel=")"
        STATE="$(echo $i | awk -F':' '{print $2}' | tr -d "state=")"
        if [ "$CHANNEL" -ne "0" ];
        then
            echo -n "        channel: $CHANNEL state: "
            case $STATE in
                "1")
                    echo -n "allowed"
                    ;;
                "2")
                    echo -n "nop_finished"
                    ;;
                "3")
                    echo -n "nop_started"
                    ;;
                "4")
                    echo -n "cac_started"
                    ;;
                "5")
                    echo -n "cac_completed"
                    ;;
                *)
                    echo -n "unknown"
            esac
            echo " ($STATE)"
        fi
    done
}

NUMBER_OF_RADIOS=$(get_key "output" "wifi_getRadioNumberOfEntries")

for i in $(eval echo "{1..$NUMBER_OF_RADIOS}")
do
    INDEX=$(($i - 1))
    echo "RADIO $INDEX:"
    echo "    ifname:" $(get_key_string output wifi_getRadioIfName $INDEX)
    echo "    enabled:" $(get_key enabled wifi_getRadioEnable $INDEX)
    echo "    channel:" $(get_key channel wifi_getRadioChannel $INDEX)
    echo "    bandwidth:" $(get_key_string bandwidth wifi_getRadioOperatingChannelBandwidth $INDEX)
    echo "    tx power:" $(get_key power wifi_getRadioTransmitPower $INDEX)
    echo "    country:" $(get_key_string country wifi_getRadioCountryCode $INDEX)
    echo "    standard:" $(get_key_string standard wifi_getRadioStandard $INDEX)
    echo "    band:" $(get_key_string band wifi_getRadioOperatingFrequencyBand $INDEX)
    echo "    possible channels:" $(get_key_string channels wifi_getRadioPossibleChannels $INDEX)
    echo "    DFS channel map:"
    get_channels_map $INDEX
done
