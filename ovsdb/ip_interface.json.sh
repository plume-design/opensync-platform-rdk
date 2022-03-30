#!/bin/bash
cat << EOF
[
    "Open_vSwitch",
    {
        "op":"insert",
        "table":"IP_Interface",
        "row": {
            "if_name": "$CONFIG_RDK_LAN_BRIDGE_NAME",
            "name": "$CONFIG_RDK_LAN_BRIDGE_NAME",
            "enable": true
       }
    },
    {
        "op":"insert",
        "table":"IP_Interface",
        "row": {
            "if_name": "$CONFIG_RDK_WAN_BRIDGE_NAME",
            "name": "$CONFIG_RDK_WAN_BRIDGE_NAME",
            "enable": true
       }
    }
]
EOF
