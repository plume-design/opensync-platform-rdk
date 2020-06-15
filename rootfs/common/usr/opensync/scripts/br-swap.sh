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
# This script is used to switch between Linux native and OVS bridges
# on a single-CPU platform

BRLAN0="brlan0"

ovsh_state()
{
    state="$1"
    if [ "`/usr/opensync/tools/ovsh u Node_State -w key==OVS.Enable value~=$state`" == 0 ]; then
        /usr/opensync/tools/ovsh i Node_State module~=rdk-rfc key~=OVS.Enable value~="$state"
    fi
}

swap_native_to_ovs()
{
    if [ `syscfg get mesh_enable` == "false" ]; then
        echo "syscfg: mesh_enable=false"
        return 0;
    fi
    if [ "`syscfg get mesh_ovs_enable`" == "false" ]; then
        echo "syscfg: mesh_ovs_enable=false"
        return 0;
    fi

    modprobe openvswitch
    if [ -e /tmp/ovs_status ]; then
        echo "ovs bridge is already up"
        return 0;
    fi
    echo "0" > /tmp/ovs_status

    if [ ! `pidof ovs-vswitchd` ] ; then
        echo "bring up ovs-vswitchd"
        mkdir -p /var/run/openvswitch
        ovs-vswitchd --pidfile=/var/run/openvswitch/ovs-vswitchd.pid --detach
        sleep 5
    fi


    aplist=`brctl show "$BRLAN0" | tail -n +2 | rev | cut  -f1 | rev`
    echo "$aplist" > /tmp/aplist

    brlan0_ip=`ifconfig "$BRLAN0" | grep Mask | tr -s ' ' | cut -d':' -f2 | cut -d' ' -f1`
    echo "$brlan0_ip" > /tmp/brlan0_ip
    brlan0_mask=`ifconfig "$BRLAN0" | grep Mask | tr -s ' ' | cut -d':' -f4 | cut -d' ' -f1`
    echo "$brlan0_mask" > /tmp/brlan0_mask
    ip route | grep "$BRLAN0" > /tmp/iproute

    for j in $aplist
    do
        brctl delif "$BRLAN0" "$j"
    done
    ifconfig "$BRLAN0" down
    brctl delbr "$BRLAN0"

    ovs-vsctl add-br "$BRLAN0"
    ifconfig "$BRLAN0" "$brlan0_ip" netmask "brlan0_mask"
    ifconfig "$BRLAN0" up
    while read i; do ip route add $i ; done < /tmp/iproute

    for j in $aplist
    do
        echo "move $j to OVS $BRLAN0"
        ovs-vsctl add-port "$BRLAN0" "$j"
    done

    brctl show
    ovs-vsctl show

    syscfg set mesh_ovs_state true
    ovsh_state true
    echo "1" > /tmp/ovs_status
    return 0
}

swap_ovs_to_native()
{
    if [ ! -e /tmp/ovs_status ]; then
        echo "ovs bridge is already down"
        return 0
    fi

    if [ -e /tmp/aplist ]; then
        aplist=`cat /tmp/aplist`
    else
        aplist=`ovs-vsctl list-ports br-home | grep -v "^br-home" | grep -v "^pgd"`
    fi

    if [ -e /tmp/brlan0_ip ]; then
        brlan0_ip=`cat /tmp/brlan0_ip`
    else
        brlan0_ip=`ifconfig "$BRLAN0" | grep Mask | tr -s ' ' | cut -d':' -f2 | cut -d' ' -f1`
    fi

    if [ -e /tmp/brlan0_mask ]; then
        brlan0_mask=`cat /tmp/brlan0_mask`
    else
        brlan0_mask=`ifconfig "$BRLAN0" | grep Mask | tr -s ' ' | cut -d':' -f4 | cut -d' ' -f1`
    fi

    if [ ! -e /tmp/iproute ]; then
        ip route | grep "$BRLAN0" > /tmp/iproute
    fi

    for j in $aplist
    do
        ovs-vsctl del-port "$BRLAN0" "$j"
    done
    ifconfig "$BRLAN0" down
    ovs-ofctl del-flows "$BRLAN0"
    ovs-ofctl add-flow "$BRLAN0" "table=0, priority=0 actions=NORMAL"
    ovs-dpctl del-flows
    ovs-vsctl del-br "$BRLAN0"

    brctl addbr "$BRLAN0"
    ifconfig "$BRLAN0" "$brlan0_ip" netmask "brlan0_mask"
    ifconfig "$BRLAN0" up
    while read i; do ip route add $i ; done < /tmp/iproute

    for j in $aplist
    do
        echo "move $j to native $BRLAN0"
        brctl addif "$BRLAN0" "$j"
    done

    ovs-vsctl show
    brctl show

    syscfg set mesh_ovs_state false
    ovsh_state false
    rm /tmp/ovs_status
    return 0
}

RET=0
case "$1" in

    "status")
        #if [ `brctl show "$BRLAN0" | wc -l` -le 1 ]; then
        if [ -e /tmp/ovs_status ]; then
            echo "OVS bridge currently in use"
        else
            echo "Linux bridge currently in use"
        fi
        ;;

    "native")
        swap_ovs_to_native
        RET=$?
        ;;

    "ovs")
        swap_native_to_ovs
        RET=$?
        ;;

    *)
        echo "usage: $0 <status | native | ovs>"
        RET=1
        ;;

esac

exit $RET
