#!/bin/sh

# This is a helper script to run batch of commands in single run.
# It is used during tests to simplify prepare/cleanup steps.
# Refer to README.md "Test plan" section of usage examples.

IP=0.0.0.0 # IP on which bs_testd listens. Use 0.0.0.0 for local invocations.
PORT=5559 # Port on which bs_testd listens.

CLIENT_MAC=aa:bb:cc:dd:ee:ff # !!CHANGEME!! set actual client STA MAC used in tests

AP_INDEX_24=0  # !!CHANGEM!! set actual 2G AP index as seen from Wifi HAL point of view
AP_INDEX_5=1   # !!CHANGME!! set actual 5G AP index as seen from Wifi HAL point of view

CFG_2="inputs/CLIENT_CFG_DEFAULT_ALLOW.in"
CFG_5="inputs/CLIENT_CFG_DEFAULT_ALLOW.in"

if [ -z "${1}" ]
then
    echo "Not enough arguments"
    exit 1
fi

if [ "${1}" = "prepare" ]
then
    ./bs_cmd -s $IP:$PORT wifi_steering_eventRegister
    ./bs_cmd -s $IP:$PORT wifi_steering_setGroup 0 inputs/AP_CFG_2.in inputs/AP_CFG_5.in
    if [ -z "${2}" ]
    then
        exit 0
    fi
elif [ "${1}" = "cleanup" ]
then
    ./bs_cmd -s $IP:$PORT wifi_steering_clientRemove 0 $AP_INDEX_24 $CLIENT_MAC
    ./bs_cmd -s $IP:$PORT wifi_steering_clientRemove 0 $AP_INDEX_5 $CLIENT_MAC
    ./bs_cmd -s $IP:$PORT wifi_steering_setGroup 0 NULL NULL
    ./bs_cmd -s $IP:$PORT wifi_steering_eventUnregister
    exit 0
elif [ "${1}" = "cleanup_no_clients" ]
then
    ./bs_cmd -s $IP:$PORT wifi_steering_setGroup 0 NULL NULL
    ./bs_cmd -s $IP:$PORT wifi_steering_eventUnregister
    exit 0
fi

# Handle client configuration

if [ "${2}" = "allow" ]
then
    CFG_2=inputs/CLIENT_CFG_DEFAULT_ALLOW.in
    CFG_5=inputs/CLIENT_CFG_DEFAULT_ALLOW.in
elif [ "${2}" = "block" ]
then
    CFG_2=inputs/CLIENT_CFG_DEFAULT_BLOCK.in
    CFG_5=inputs/CLIENT_CFG_DEFAULT_BLOCK.in
elif [ "${2}" = "single-gw" ]
then
    CFG_2=inputs/CLIENT_CFG_DEFAULT_SINGLE_GW_2G.in
    CFG_5=inputs/CLIENT_CFG_DEFAULT_SINGLE_GW_5G.in
elif [ "${2}" = "multi-ap" ]
then
    CFG_2=inputs/CLIENT_CFG_DEFAULT_MULTI_AP_2G.in
    CFG_5=inputs/CLIENT_CFG_DEFAULT_MULTI_AP_5G.in
elif [ "${2}" = "pre-assoc" ]
then
    CFG_2=inputs/CLIENT_CFG_PRE_ASSOC_TEST.in
    CFG_5=inputs/CLIENT_CFG_PRE_ASSOC_TEST.in
elif [ "${2}" = "xings" ]
then
    CFG_2=inputs/CLIENT_CFG_XINGS_TEST.in
    CFG_5=inputs/CLIENT_CFG_XINGS_TEST.in
fi

./bs_cmd -s $IP:$PORT wifi_steering_clientSet 0 $AP_INDEX_24 $CLIENT_MAC $CFG_2
./bs_cmd -s $IP:$PORT wifi_steering_clientSet 0 $AP_INDEX_5 $CLIENT_MAC $CFG_5

