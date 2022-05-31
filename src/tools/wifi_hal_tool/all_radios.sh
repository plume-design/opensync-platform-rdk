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
