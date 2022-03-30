if [ "$CONFIG_RDK_EXTENDER" == "y" ]; then
    # do nothing if not set
    echo '["Open_vSwitch"]'
    exit
fi

cat << EOF
[
    "Open_vSwitch",
$(for i in $(eval echo $CONFIG_TARGET_ETH0_NAME \
                       $CONFIG_TARGET_ETH1_NAME \
                       $CONFIG_TARGET_ETH2_NAME \
                       $CONFIG_TARGET_ETH3_NAME \
                       $CONFIG_TARGET_ETH4_NAME \
                       $CONFIG_TARGET_ETH5_NAME)
do
cat <<EOI
    {
        "op":"insert",
        "table":"Wifi_Inet_Config",
        "row":
        {
            "if_name": "$i",
            "if_type": "eth",
            "enabled": false,
            "mac_reporting": true
        }
    },
EOI
done)
    { "op": "comment", "comment": "" }
]
EOF
