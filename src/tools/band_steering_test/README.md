RDK HAL Band Steering API Test
------------------------------

Components:

* bs_testd - a daemon emulating OpenSync BM (Band Steering Manager) context. It must be run on target in order to start BS tests.
             Its role is to execute Band Steering Wifi HAL functions and deliver BS events. It receives commands over TCP socket
             from the 'bs_cmd' command-line utility. It can output events to STDOUT or send them over UDP socket.
* bs_cmd -   a command-line utility to trigger calls of Band Steering HAL functions. Each execution is a one-shot operation.
             It is required to start the 'bs_testd' before using this utility. This utility can be run directly on target or
             on a host system if TCP access to target is available.

Build

The test is part of OpenSync build system. Put it inside platform/rdk/src/tools and build OpenSync. Two binaries should be
created: bs_testd and bs_cmd
To build host-side bs_cmd, simply do: gcc bs_cmd.c -o bs_cmd

Receiving events

To get events remotely on socket, you can use nc (example):
$ nc -u -l 5558

and then, on target (example) run the deamon:
$ ./bs_testd -B -s 192.168.1.1:5558 > /dev/null

Alternatively, run bs_testd in foreground, so the events are printed to the stdout:
$ ./bs_testd &

Commands:

Each Wifi HAL invocation is a separate command sent using bs_cmd tool. All parameters to the
HAL are passed as command-line arguments, except complex input structures which are passed
as files (by default located under inputs/ directory). Please refer to wifi_hal.h file for details
of input parameters meaning.

Example (remote setup):
Run on target:
$ ./bs_testd -B -b 10.24.3.11:5559 -s 192.168.1.1:5558
or for debug:
$ ./bs_testd -v -F 10.24.3.11:5559 -s 192.168.1.1:5558

On Host (192.168.1.1):
$ nc -u -l 5558

on Host (other terminal):
$ ./bs_cmd -s 10.24.3.11:5559 wifi_steering_eventRegister
$ ./bs_cmd -s 10.24.3.11:5559 wifi_steering_setGroup 0 inputs/AP_CFG.in inputs/AP_CFG.in
$ ./bs_cmd -s 10.24.3.11:5559 wifi_steering_clientSet 0 0 aa:bb:cc:dd:ee:ff inputs/CLIENT_CFG_DEFAULT_SINGLE_AP_2G.in
$ ./bs_cmd -s 10.24.3.11:5559 wifi_steering_clientSet 0 8 aa:bb:cc:dd:ee:ff inputs/CLIENT_CFG_DEFAULT_SINGLE_AP_5G.in
$ ./bs_cmd -s 10.24.3.11:5559 wifi_steering_clientMeasure 0 0 aa:bb:cc:dd:ee:ff
$ ./bs_cmd -s 10.24.3.11:5559 exit

Example (local setup)
On target:
$ ./bs_testd &

On target:
$ ./bs_testd &
$ ./bs_cmd wifi_steering_eventRegister
$ ./bs_cmd wifi_steering_setGroup 0 inputs/AP_CFG.in inputs/AP_CFG.in
$ ./bs_cmd wifi_steering_clientSet 0 0 aa:bb:cc:dd:ee:ff inputs/CLIENT_CFG_DEFAULT_SINGLE_AP_2G.in
$ ./bs_cmd wifi_steering_clientSet 0 8 aa:bb:cc:dd:ee:ff inputs/CLIENT_CFG_DEFAULT_SINGLE_AP_5G.in
$ ./bs_cmd wifi_steering_clientMeasure 0 0 aa:bb:cc:dd:ee:ff
$ >/bs_cmd 10.24.3.11:5559 exit

TEST PLAN

Prerequisites:
 * Client supporting BTM and RRM
 * Client without support of BTM and RRM (a.k.a legacy STA)
 * Client supporting VHT
 * Client not supporting VHT
 * Client 2.4GHz only
 * Airlog sniffer for 2.4GHz and for 5GHz band
 * Controlled (deterministic) test environment, e.g. Test House or shield-room with programmable attenuators connected to clients.
 * All clients need to be able to be put into the pre-defined SNR conditions. The reference SNR can be measured using airlog sniffer
   (if SNR or NF + RSSI readings are available) or using reference router running OpenSync. Clients should be able to be put into
   SNR=10, SNR=30, SNR=45 versus Device Under Test (DUT) signal conditions.

Test configuration:
 * Prepare MAC addresses of all the clients, the 2.4G DUT Home-AP and the 5G DUT Home-AP.
 * Modify the MAC addresses of neighbor APs in inputs/BTM_REQEUST.in file (bssid field for each candidate). The first
   candidate's bssid should be a bssid of AP to which STA should be steered (eg. 5G DUT Home-AP MAC). This will be
   modified during tests as well. The second bssid can be any MAC (it is needed only to verify if more than one
   candidate is included correctly in the BTM frame).
 * Modify the action.sh script - set correct IP and PORT when using remote access (you can leave it unchanged if running both tools
   directly on target), set CLIENT_MAC field and update the AP_INDEX_24 and AP_INDEX_5 fields to reflect actual AP indices used by
   Wifi HAL (eg. 0 for 2.4G & 8 for 5G).
 * Modify "apIndex" field in inputs/AP_CFG_2.in and inputs/AP_CFG_5.in with actual AP index used by Wifi HAL.
 * Run bs_testd on the DUT. The preferred way is to run it with -b and -s options so it can be orchestrated remotely.
   Example: ./bs_testd -B -b 10.129.4.33:5559 -s 192.168.1.1:5558 > /dev/null (you need to adjust IP addresses to your network
   setup)
   Debug example: ./bs_testd -v -b 10.129.4.33:5559 -s 192.168.1.1:5558 (you need to adjust UP addresses to your network
   setup)
 * On host machine listen for messages on socket in dedicated terminal.
   Example: nc -u -l 5558 | tee bs_event.log
 * Alternatively, run bs_testd on DUT w/o remote access: ./bs_testd


Test 1 - CONNECT/DISCONNECT events:
Verify that steering CONNECT and DISCONNECT events are reported and that capabilities reported in CONNECT event are correct.

Steps:
 1. ./action.sh prepare
 2. Start airlog sniffers on both bands
 3. Associate BTM/RRM Client to 2.4GHz AP.
 4. Disconnect Client
 5. Repeat (one-time) steps 3-4 for all other clients types (legacy, ht, vht).
 6. ./bs_cmd [-s ip:port] exit
 7. ./action.sh cleanup_no_clients

Corresponding pass criteria for each step:
 1. No errors reported
 2. Able to see packets in the airlog
 3. CONNECT event received with capabilities matching association frame from airlog
 4. DISCONNECT event received with Client's MAC address and "DISCONNECT_SOURCE_REMOTE" set as source.
 5. Same as 3-4.
 6. No errors
 7. No errors

Test 2 - Pre-association blocking
Verify that STA is blocked when trying to associate while being outside the HWM and LWM range but is able to associate when the SNR
is within the range.

Steps:
 1. ./action.sh prepare pre-assoc
 2. Start airlog sniffers on both bands
 3. Put Client STA in SNR=10 (for 2.4G) conditions and try to associate to the DUT
 4. Put Client STA in SNR=45 (for 2.4G) conditions and try to associate to the DUT
 5. Put Client STA in SNR=30 (for 2.4G) conditions and try to associate to the DUT
 6. Repeat (one-time) steps 3-5 but for 5G band.
 7. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Able to see packets in the airlog
 3. PROBE events 'blocked' with SNR=10 (+/-6) received. No Probe responses seen in the airlog.
 4. PROBE events 'blocked' with SNR=40 (+/-6) received. No Probe responses seen in the airlog.
 5. PROBE events 'not blocked' with SNR=30 (+/-6) received. CONNECT event with correct capabilities (as in Test1) received. Client associated successfully.
 6. Same as 3-5
 7. No errors

Test 3 - BTM request sending
Verify that GW is able to send a valid BTM request frame

Steps:
 1. ./action.sh prepare allow
 2. Start airlog sniffers on both bands
 3. Associate BTM capable station to 2.4GH AP
 4. Modify inputs/BTM_REQUST.in so it contains 5GHz AP BSSID, current 5GHz AP BSSID channel and one additional (fake) neighbor BSSID.
 5. ./bs_cmd [-s ip:port] wifi_setBTMRequest <2.4G AP INDEX> <CLIENT MAC> inputs/BTM_REQUEST.in
 6. Modify inputs/BTM_REQUST.in so it contains 2.4GHz AP BSSID, current 2.4GHz AP BSSID channel and one additional (fake) neighbor BSSID.
 7. ./bs_cmd [-s ip:port] wifi_setBTMRequest <5G AP INDEX> <CLIENT MAC> inputs/BTM_REQUEST.in
 8. Repeat (one-time) steps 4-7
 9. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Able to see packets in the airlog
 3. Device associated successfully, correct (see Test1) CONNECT event received
 4. The inputs/BTM_request.in has valid data
 5. Correct BTM frame is seen in the airlog (wlan.fixed.category_code == 10). Frame should be compared to
    inputs/BTM_REQUEST.in content. Device associates to 5GHz AP. Correct (see Test1) CONNECT event received.
 6. The inputs/BTM_request.in has valid data
 7. Correct BTM frame is seen in the airlog (wlan.fixed.category_code == 10). Frame should be compared to
    inputs/BTM_REQUEST.in content. Device associates to 2.4GHz AP. Correct (see Test1) CONNECT event received.
 8. Same as 4-7
 9. no errors

Test 4 - Crossings
Verify if HWM/LWM based crossing events are reported correctly.
The actual watermarks values can be modified by editing the inputs/CLIENT_CFG_XINGS_TEST.in file.

Steps:
 1. ./action.sh prepare xings
 2. Put client STA in SNR=10 Associate client to 2.4G AP
 3. Start network traffic on the client STA.
 4. Move client STA to SNR=30 conditions
 5. Move client STA to SNR=45 conditions
 6. Move client STA to SNR=30 conditions
 7. Move client STA to SNR=10 conditions
 8. Repeat (one-time) steps 2-7 but on 5GHz band
 9. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Device associated successfully, correct (see Test1) CONNECT event received
 3. Network traffic ongoing
 4. XING EVENT LWM=1 reported on band on which client is associated. No other xings reported
 5. XING EVENT HWM=1 reported on band on which client is associated. No other xings reported
 6. XING EVENT HWM=2 reported on band on which client is associated. No other xings reported
 7. XING EVENT LWM=2 reported on band on which client is associated. No other xings reported
 8. Same as 2-7 but on 5GHz band
 9. No errors

Test 5 - Legacy kick
Verify if device can be kicked using legacy method

Steps:
 1. ./action.sh prepare allow
 2. Start airlog sniffers on both bands
 3. Associate Legacy Client to 2.4GHz AP
 4. ./bs_cmd [-s ip:port] wifi_steering_clientDisconnect 0 <2.4 AP INDEX> <CLIENT MAC> 1 0
 5. Associate Legacy Client to 2.4GHz AP
 6. ./bs_cmd [-s ip:port] wifi_steering_clientDisconnect 0 <2.4 AP INDEX> <CLIENT MAC> 2 0
 7. Repeat (one-time) steps 3-5 but on 5GHz band
 8. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Able to see packets in the airlog
 3. Device associated successfully, correct (see Test1) CONNECT event received
 4. Disassociation frame seen in the airlog, client is disconnected
 5. Device associated successfully, correct (see Test1) CONNECT event received
 6. Deauthentication frame seen in the airlog, client is disconnected
 7. Same As 3-5 but on 5GHz band
 8. No errors

Test 6 - Sending RRM frame
Verify if Radio Management frame is sent correctly

Steps:
 1. ./action.sh prepare allow
 2. Start airlog sniffers on both bands
 3. Associate RM-capable client to 2.4GHz AP
 4. ./bs_cmd [-s ip:port] wifi_setRMBeaconRequest <2.4GHz AP INDEX> <CLIENT MAC> 0 inputs/RM_REQUEST.in
 5. Repeat (one-time) steps 3-4 but on 5GHz band
 6. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Able to see packets in the airlog
 3. Device associated successfully, correct (see Test1) CONNECT event received
 4. RM frame seen in the airlog (filter: wlan.fixed.category_code == 5). The values in the frame
    should be compared to content of inputs/RM_REQUEST.in file.
 5. Same as 3-4 but on 5GHz band
 6. No errors

Test 7 - Activity reporting
Verify if ACTIVITY event is reported correctly

Steps:
 1. ./action.sh prepare allow
 2. Associate Legacy Client to 2.4GHz AP
 3. Start network traffic > 2000bps, for example iperf, or wget <external test server> for 30 seconds
 4. Stop network traffic, wait 60 seconds
 5. Start network traffic > 2000bps, for example iperf, or wget <external test server> for 30 seconds
 6. Stop network traffic, wait 60 seconds
 7. Repeat (one-time) steps 2-6 but on 5GHz band
 8. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Device associated successfully, correct (see Test1) CONNECT event received
 3. ACTIVITY (active=true) event reported
 4. ACTIVITY (active=false) event reported.
 5. ACTIVITY (active=true) event reported
 6. ACTIVITY (active=false) event reported.
 7. Same as 2-6 but on 5GHz band
 8. No errors


Test 8 - Client measure
Verify if direct SNR measurement mechanism works

Steps:
 1. ./action.sh prepare allow
 2. Put Client STA in SNR=10 and Associate to 2.4GHz AP
 3. Run network traffic on Client
 4. ./bs_cmd [-s ip:port] wifi_steering_clientMeasure 0 <2.4GHz AP INDEX> <CLIENT MAC>
 5. Move Client STA to SNR=30
 6. ./bs_cmd [-s ip:port] wifi_steering_clientMeasure 0 <2.4GHz AP INDEX> <CLIENT MAC>
 7. Move Client STA to SNR=45
 8. ./bs_cmd [-s ip:port] wifi_steering_clientMeasure 0 <2.4GHz AP INDEX> <CLIENT MAC>
 9. Move Client STA to SNR=10
 10. ./bs_cmd [-s ip:port] wifi_steering_clientMeasure 0 <2.4GHz AP INDEX> <CLIENT MAC>
 11. Repeat (one-time) steps 2-10 but on 5GHz band
 12. ./action.sh cleanup

Corresponding pass criteria for each step:
 1. No errors reported
 2. Device associated successfully, correct (see Test1) CONNECT event received
 3. Traffic ongoing on Client
 4. EVENT_RSSI event with SNR=10 (+/-3) received
 5. Client in SNR=30 conditions
 6. EVENT_RSSI event with SNR=30 (+/-3) received
 7. Client in SNR=45 conditions
 8. EVENT_RSSI event with SNR=45 (+/-3) received
 9. Client in SNR=10 conditions
 10. EVENT_RSSI event with SNR=10 (+/-3) received
 11. Same as 2-10 but on 5GHz band
 12. No errors

Test 9 - Remove client configuration
Verify if client's configuration can be removed

Steps:
 1. ./action.sh prepare block
 2. Try associate Client STA to 2.4GHz AP
 3. Try associate Client STA to 5GHz AP
 4. ./bs_cmd [-s ip:port] wifi_steering_clientRemove 0 <2.4GHz AP INDEX> <CLIENT MAC>
 5. Associate Client STA to 2.4GHz AP
 6. Disconnect Client STA from 2.4GHz AP
 7. ./bs_cmd [-s ip:port] wifi_steering_clientRemove 0 <5GHz AP INDEX> <CLIENT MAC>
 8. Associate Client STA to 5GHz AP
 9. Disconnect Client STA from 5GHz AP
 10. ./action.sh cleanup_no_clients

Corresponding pass criteria for each step:
 1. No errors reported
 2. Client cannot associate
 3. Client cannot associate
 4. No errors
 5. Device associated successfully, correct (see Test1) CONNECT event received,
    no other events received for that client
 6. DISCONNECT event received
 7. No errors
 8. Device associated successfully, correct (see Test1) CONNECT event received,
    no other events received for that client
 9. DISCONNECT event received
 10. No errors

Test 10 - Remove steering group
Verify if steering group can be removed

Steps:
 1. ./action.sh prepare block
 2. Try associate Client STA to 2.4GHz AP
 3. Try associate Client STA to 5GHz AP
 4. ./bs_cmd [-s ip:port] wifi_steering_setGroup 0 NULL NULL
 5. Associate Client STA to 2.4GHz AP
 6. Disconnect Client STA from 2.4GHz AP
 7. Associate Client STA to 5GHz AP
 8. Disconnect Client STA from 5GHz AP
 9. Watch for BS events for 1 minute
 10. ./bs_cmd [-s ip:port] wifi_steeringUnregister

Corresponding pass criteria for each step:
 1. No errors reported
 2. Client cannot associate
 3. Client cannot associate
 4. No errors
 5. Client associated successfully, no events received
 6. Client disassociated successfully, no events received
 7. Client associated successfully, no events received
 8. Client disassociated successfully, no events received
 9. No events received
 10. No errors
