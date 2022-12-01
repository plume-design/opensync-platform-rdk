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


#
# Compare Wifi_VIF_Config and Wifi_VIF_State to spot differences
#

OVSH=/usr/opensync/tools/ovsh

ifnames=$($OVSH s Wifi_VIF_Config if_name -r)
for param in $($OVSH s Wifi_VIF_Config -T | sed -n '2,+0p' | sed "s/|//g"); do
    if [ "$param" == "_uuid" ] || [ "$param" == "_version" ]; then
        continue
    fi

    for ifname in $ifnames; do
        config_value=$($OVSH s Wifi_VIF_Config -w if_name==$ifname $param -r)
        state_value=$($OVSH s Wifi_VIF_State -w if_name==$ifname $param -r 2>/dev/null)
        if [ "$config_value" != "[\"set\",[]]" ] && [ "$config_value" != "[\"map\",[]]" ] && [ "$config_value" != "$state_value" ]; then
            echo "if_name = $ifname, param=$param, config_value=$config_value, state_value=$state_value"
        fi
    done
done
