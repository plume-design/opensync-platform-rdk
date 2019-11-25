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
# RDK configuration
#
##############################################################################

CPU_TYPE = $(RDK_TARGET_ARCH)

CFLAGS += -Wno-error=cpp
CFLAGS += -I$(PKG_CONFIG_SYSROOT_DIR)/usr/include/protobuf-c

# Yocto takes care of packaging stripped and unstripped binaries on its own,
# and generates 2 packages: with and without debug symbols.
# Also, Yocto fills in STRIP, which we use internally for stripping,
# which conflicts with Yocto's assumptions.
STRIP=

#
# Features (1 = Enabled, 0 = Disabled)
#
QCA_WIFI ?= 0
QTN_WIFI ?= 0
BCM_WIFI ?= 0
WPA_CLIENTS ?= 0
RDK_LOGGER ?= 1

ifeq ($(QCA_WIFI),1)
WPA_CLIENTS=1
RDK_CFLAGS += -DQCA_WIFI
endif

ifeq ($(QTN_WIFI),1)
RDK_CFLAGS += -DQTN_WIFI
endif

ifeq ($(BCM_WIFI),1)
RDK_CFLAGS += -DBCM_WIFI
endif

ifeq ($(WPA_CLIENTS),1)
RDK_CFLAGS += -DWPA_CLIENTS
endif

ifeq ($(RDK_LOGGER),1)
RDK_CFLAGS += -DLOG_ENABLE_RDKLOG
endif

CFLAGS += $(RDK_CFLAGS)
LDFLAGS += $(RDK_LDFLAGS)

#
# Debug
#
$(info Included $(lastword $(MAKEFILE_LIST)))
$(info RDK_OEM=$(RDK_OEM))
$(info RDK_TARGET=$(RDK_TARGET))
$(info RDK_CFLAGS=$(RDK_CFLAGS))
$(info RDK_LDFLAGS=$(RDK_LDFLAGS))
$(info CFLAGS=$(CFLAGS))
$(info LDFLAGS=$(LDFLAGS))

#
# RDK OVSDB/PROFILE configuration
#

ifeq ($(CONTROLLER_ADDR),)
ifneq ($(CONTROLLER_HOST),)
CONTROLLER_ADDR = $(CONTROLLER_PROTO):$(CONTROLLER_HOST):$(CONTROLLER_PORT)
endif
endif

# Clear profile so that nothing is appended to version string
export IMAGE_DEPLOYMENT_PROFILE = none

# INSTALL_DIR provided by recipe
# INSTALL_ROOTFS_DIR required by core/build/rootfs.mk
INSTALL_ROOTFS_DIR = $(INSTALL_DIR)

# OVSDB files target dir
OVSDB_DB_DIR            = $(INSTALL_PREFIX)/etc
OVSDB_SCHEMA_DIR        = $(INSTALL_PREFIX)/etc

ROOTFS_COMPONENTS = common
ifneq ($(RDK_TARGET),)
ROOTFS_COMPONENTS += $(RDK_TARGET)/common
ifneq ($(RDK_OEM),)
ROOTFS_COMPONENTS += $(RDK_TARGET)/$(RDK_OEM)
endif
endif

ifneq ($(RDK_MODEL),)
ROOTFS_COMPONENTS += model/$(RDK_MODEL)
endif

KCONFIG_TARGET ?= platform/rdk/kconfig/RDK

all: build_all rootfs

install_info:
	@echo ===============
	@echo INSTALL_DIR=$(INSTALL_DIR)
	@echo INSTALL_ROOTFS_DIR=$(INSTALL_ROOTFS_DIR)
	@echo === install ===

install: install_info rootfs-install-only
	@echo === done ===
