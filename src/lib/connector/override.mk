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
# unit override for connector
#
##############################################################################
UNIT_DISABLE := $(if $(CONFIG_MANAGER_XM),n,y)

UNIT_CFLAGS += -I$(OVERRIDE_DIR)/inc
UNIT_CFLAGS += -I${STAGING_INCDIR}/dbus-1.0
UNIT_CFLAGS += -I${STAGING_LIBDIR}/dbus-1.0/include
UNIT_CFLAGS += -I${STAGING_INCDIR}/ccsp
UNIT_CFLAGS += -D_ANSC_LINUX -D_ANSC_USER -D_ANSC_LITTLE_ENDIAN_
UNIT_LDFLAGS := $(SDK_LIB_DIR) -lccsp_common

UNIT_SRC_TOP := $(OVERRIDE_DIR)/src/connector_main.c
UNIT_SRC_TOP += $(OVERRIDE_DIR)/src/connector_dm.c

UNIT_DEPS += src/lib/kconfig
UNIT_DEPS += src/lib/log
UNIT_DEPS += src/lib/schema
UNIT_DEPS += src/lib/common
UNIT_DEPS += src/lib/ovsdb

UNIT_EXPORT_CFLAGS := $(UNIT_CFLAGS)
UNIT_EXPORT_LDFLAGS := $(UNIT_LDFLAGS)
