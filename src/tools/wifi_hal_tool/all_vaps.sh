#!/bin/sh

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
