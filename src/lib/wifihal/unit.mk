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

UNIT_NAME := wifihal

UNIT_DISABLE := !RDKB

UNIT_TYPE   := LIB

# List of source files, relative to the unit folder
UNIT_SRC    := wifihal.c
UNIT_SRC    += wifihal_radio.c
UNIT_SRC    += wifihal_vif.c
UNIT_SRC    += wifihal_security.c
UNIT_SRC    += wifihal_acl.c
UNIT_SRC    += wifihal_clients.c
UNIT_SRC    += wifihal_sync.c
UNIT_SRC    += wifihal_cloud_mode.c
UNIT_SRC    += wifihal_stats.c
UNIT_SRC    += wifihal_health.c
UNIT_SRC    += wifihal_maclearn.c
ifeq ($(WPA_CLIENTS),1)
UNIT_SRC    += wifihal_clients_wpa.c
else
UNIT_SRC    += wifihal_clients_hal.c
endif

UNIT_CFLAGS := -I$(UNIT_PATH)/inc
UNIT_CFLAGS += -DENABLE_MESH_SOCKETS

UNIT_LDFLAGS := $(SDK_LIB_DIR)  -lhal_wifi -lrt

ifeq ($(WPA_CLIENTS),1)
UNIT_LDFLAGS += $(RDK_FILE_WPA_CTRL_A)
endif

ifeq ($(QCA_WIFI),1)
UNIT_CFLAGS += -DHOSTAPD_RECONFIG
UNIT_CFLAGS += -DWAR_VIF_DISABLE
endif


UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)

UNIT_CFLAGS += -DCONTROLLER_ADDR="\"$(shell echo -n $(CONTROLLER_ADDR))\""

UNIT_DEPS   := src/lib/ds
UNIT_DEPS   += src/lib/common
UNIT_DEPS   += src/lib/evsched
UNIT_DEPS   += src/lib/schema
UNIT_DEPS   += src/lib/const
UNIT_DEPS   += $(PLATFORM_DIR)/src/lib/devinfo

UNIT_DEPS_CFLAGS += src/lib/target
UNIT_DEPS_CFLAGS += src/lib/datapipeline
