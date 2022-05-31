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

get_acl_mode()
{
    case $1 in
        "0")
            echo -n "none"
            ;;
        "1")
            echo -n "whitelist"
            ;;
        "2")
            echo -n "blacklist"
            ;;
        "3")
            echo -n "flush"
            ;;
        *)
            echo -n "unknown"
    esac

    echo " ($1)"
}

for i in $(eval echo "{1..$NUMBER_OF_VAPS}")
do
    INDEX=$(($i - 1))
    echo "VAP $INDEX:"
    echo "    ifname:" $(get_key_string ifname wifi_getApName $INDEX)
    echo "    radio index:" $(get_key radioIndex wifi_getSSIDRadioIndex $INDEX)
    echo "    enabled:" $(get_key enabled wifi_getSSIDEnable $INDEX)
    echo "    bssid:" $(get_key_string mac wifi_getBaseBSSID $INDEX)
    echo "    ssid:" $(get_key_string ssid wifi_getSSIDName $INDEX)
    echo "    security:" $(get_key_string mode wifi_getApSecurityModeEnabled $INDEX)
    echo "    password:" $(get_key_string passphrase wifi_getApSecurityKeyPassphrase $INDEX)
    echo "    advertisement:" $(get_key enabled wifi_getApSsidAdvertisementEnable $INDEX)
    echo "    btm:" $(get_key btm wifi_getBSSTransitionActivation $INDEX)
    echo "    rrm:" $(get_key rrm wifi_getNeighborReportActivation $INDEX)
    echo "    acl mode:" $(get_acl_mode $(get_key acl_mode wifi_getApMacAddressControlMode $INDEX))
    echo "    acl devices:" $(get_key_string acl_list wifi_getApAclDevices $INDEX)
done
