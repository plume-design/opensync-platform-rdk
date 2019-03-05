/*
Copyright (c) 2017, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DMCLI_H_INCLUDED
#define DMCLI_H_INCLUDED

#define DMCLI_MAX_PATH              128

#define DMCLI_ERT_MODEL_NUM         "Device.DeviceInfo.ModelName"
#define DMCLI_ERT_SERIAL_NUM        "Device.DeviceInfo.SerialNumber"
#define DMCLI_ERT_SOFTWARE_VER      "Device.DeviceInfo.SoftwareVersion"
#define DMCLI_ERT_HARDWARE_VER      "Device.DeviceInfo.HardwareVersion"
#define DMCLI_ERT_CM_MAC            "Device.DeviceInfo.X_COMCAST-COM_CM_MAC"
#define DMCLI_ERT_CM_IP             "Device.DeviceInfo.X_COMCAST-COM_CM_IP"
#define DMCLI_ERT_WAN_MAC           "Device.DeviceInfo.X_COMCAST-COM_WAN_MAC"
#define DMCLI_ERT_WAN_IP            "Device.DeviceInfo.X_COMCAST-COM_WAN_IP"
#define DMCLI_ERT_WAN_IPv6          "Device.DeviceInfo.X_COMCAST-COM_WAN_IPv6"
#define DMCLI_ERT_HOME_IP           "Device.X_CISCO_COM_DeviceControl.LanManagementEntry.1.LanIPAddress"
#define DMCLI_ERT_HOME_MAC          "Device.Ethernet.Link.1.MACAddress"
#define DMCLI_ERT_MESH_ENABLE       "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.Mesh.Enable"
#define DMCLI_ERT_MESH_STATE        "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.Mesh.State"
#define DMCLI_ERT_MESH_STATUS       "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.Mesh.Status"
#define DMCLI_ERT_MESH_URL          "Device.DeviceInfo.X_RDKCENTRAL-COM_xOpsDeviceMgmt.Mesh.URL"

extern bool     dmcli_eRT_getv(const char *path, char *dest, size_t destsz, bool empty_ok);

#endif /* DMCLI_H_INCLUDED */
