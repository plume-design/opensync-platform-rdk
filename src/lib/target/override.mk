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

##############################################################################
#
# RDK unit override for target library
#
##############################################################################

UNIT_SRC := $(TARGET_COMMON_SRC)

UNIT_SRC_DIR := $(OVERRIDE_DIR)/src

UNIT_SRC_TOP :=
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/target.c
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/clients.c
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/radio.c
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/vif.c
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/stats.c
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/log.c

ifneq ($(CONFIG_RDK_DISABLE_SYNC),y)
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/sync.c
endif

UNIT_SRC_TOP += $(if $(CONFIG_RDK_WIFI_HAL_VERSION_3_PHASE2), $(UNIT_SRC_DIR)/bsal.c, $(UNIT_SRC_DIR)/bsal_legacy.c)
UNIT_SRC_TOP += $(UNIT_SRC_DIR)/cloud_config.c
UNIT_SRC_TOP += $(if $(CONFIG_RDK_WPS_SUPPORT), $(UNIT_SRC_DIR)/wps.c)
UNIT_SRC_TOP += $(if $(CONFIG_RDK_MULTI_AP_SUPPORT), $(UNIT_SRC_DIR)/multi_ap.c)

UNIT_CFLAGS  += -I$(OVERRIDE_DIR)/inc
UNIT_CFLAGS  += -DENABLE_MESH_SOCKETS
UNIT_CLFAGS  += -DTARGET_H=\"target_RDKB.h\"

UNIT_DEPS    += $(PLATFORM_DIR)/src/lib/devinfo
UNIT_DEPS    += src/lib/schema
UNIT_DEPS    += src/lib/const
UNIT_DEPS    += src/lib/osp

UNIT_DEPS_CFLAGS += src/lib/ovsdb # Needed for ovsdb_utils.h
UNIT_DEPS_CFLAGS += src/lib/osn   # Needed for osn_dhcp.h

ifeq ($(RDK_LOGGER),1)
UNIT_DEPS    += $(PLATFORM_DIR)/src/lib/pl2rl
endif

UNIT_LDFLAGS := $(SDK_LIB_DIR)  -lhal_wifi -lrt
UNIT_CFLAGS += -DCONTROLLER_ADDR="\"$(shell echo -n $(CONTROLLER_ADDR))\""

UNIT_EXPORT_CFLAGS  := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)
