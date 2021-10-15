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
KCONFIG_ENV_FILE=$(dirname $(dirname "$(readlink -f "$0")"))/etc/kconfig
. "$KCONFIG_ENV_FILE"

LAN_BRIDGE=$CONFIG_RDK_LAN_BRIDGE_NAME

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


    aplist=`brctl show "$LAN_BRIDGE" | tail -n +2 | rev | cut  -f1 | rev`
    echo "$aplist" > /tmp/aplist

    brlan0_ip=`ifconfig "$LAN_BRIDGE" | grep Mask | tr -s ' ' | cut -d':' -f2 | cut -d' ' -f1`
    brlan0_ipv6=`ip -6 addr show dev "$LAN_BRIDGE" | grep global | tr -s ' ' | cut -d' ' -f3`
    echo "$brlan0_ip" > /tmp/brlan0_ip
    echo "$brlan0_ipv6" > /tmp/brlan0_ipv6
    brlan0_mask=`ifconfig "$LAN_BRIDGE" | grep Mask | tr -s ' ' | cut -d':' -f4 | cut -d' ' -f1`
    echo "$brlan0_mask" > /tmp/brlan0_mask
    ip route | grep "$LAN_BRIDGE" > /tmp/iproute
    ip -6 route | grep "$LAN_BRIDGE" > /tmp/iproute6

    for j in $aplist
    do
        brctl delif "$LAN_BRIDGE" "$j"
    done
    ifconfig "$LAN_BRIDGE" down
    brctl delbr "$LAN_BRIDGE"

    ovs-vsctl add-br "$LAN_BRIDGE"
    ifconfig "$LAN_BRIDGE" "$brlan0_ip" netmask "$brlan0_mask"
    ifconfig "$LAN_BRIDGE" up
    ip -6 address add "$brlan0_ipv6" dev "$LAN_BRIDGE"
    while read i; do ip route add $i ; done < /tmp/iproute
    while read k; do ip -6 route add $k ; done < /tmp/iproute6

    for j in $aplist
    do
        echo "move $j to OVS $LAN_BRIDGE"
        ovs-vsctl add-port "$LAN_BRIDGE" "$j"
    done

    brctl show
    ovs-vsctl show

    syscfg set mesh_ovs_state true
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
        brlan0_ip=`ifconfig "$LAN_BRIDGE" | grep Mask | tr -s ' ' | cut -d':' -f2 | cut -d' ' -f1`
    fi

    if [ -e /tmp/brlan0_ipv6 ]; then
        brlan0_ipv6=`ip -6 addr show dev "$LAN_BRIDGE" | grep global | tr -s ' ' | cut -d' ' -f3`
    else
        brlan0_ipv6=`ip -6 addr show dev "$LAN_BRIDGE" | grep global | tr -s ' ' | cut -d' ' -f3`
    fi

    if [ -e /tmp/brlan0_mask ]; then
        brlan0_mask=`cat /tmp/brlan0_mask`
    else
        brlan0_mask=`ifconfig "$LAN_BRIDGE" | grep Mask | tr -s ' ' | cut -d':' -f4 | cut -d' ' -f1`
    fi

    if [ ! -e /tmp/iproute ]; then
        ip route | grep "$LAN_BRIDGE" > /tmp/iproute
    fi

    if [ ! -e /tmp/iproute6 ]; then
        ip -6 route | grep "$LAN_BRIDGE" > /tmp/iproute6
    fi

    for j in $aplist
    do
        ovs-vsctl del-port "$LAN_BRIDGE" "$j"
    done
    ifconfig "$LAN_BRIDGE" down
    ovs-ofctl del-flows "$LAN_BRIDGE"
    ovs-ofctl add-flow "$LAN_BRIDGE" "table=0, priority=0 actions=NORMAL"
    ovs-dpctl del-flows
    ovs-vsctl del-br "$LAN_BRIDGE"

    brctl addbr "$LAN_BRIDGE"
    ifconfig "$LAN_BRIDGE" "$brlan0_ip" netmask "$brlan0_mask"
    ifconfig "$LAN_BRIDGE" up
    ip -6 address add "$brlan0_ipv6" dev "$LAN_BRIDGE"
    while read i; do ip route add $i ; done < /tmp/iproute
    while read k; do ip route add $k ; done < /tmp/iproute6

    for j in $aplist
    do
        echo "move $j to native $LAN_BRIDGE"
        brctl addif "$LAN_BRIDGE" "$j"
    done

    ovs-vsctl show
    brctl show

    syscfg set mesh_ovs_state false
    rm /tmp/ovs_status
    return 0
}

RET=0
case "$1" in

    "status")
        #if [ `brctl show "$LAN_BRIDGE" | wc -l` -le 1 ]; then
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
