#/**
# * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
# * This program is free software; you can redistribute it and/or modify it under the terms of the
# * GNU General Public License as published by the Free Software Foundation; version 2.
# *
# * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
# * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# * General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License along with this program; if
# * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# * 02110-1301, USA.
# */


MODULE      := slash

KDIR        ?= /lib/modules/$(shell uname -r)/build
PWD         := $(shell pwd)

QDMA_DRV_ROOT := ../submodules/qdma_drv/QDMA/linux-kernel/driver

obj-m          := $(MODULE).o
$(MODULE)-objs := $(MODULE)_main.o $(MODULE)_ctldev.o $(MODULE)_pcie.o $(MODULE)_dmabuf.o $(MODULE)_hotplug.o $(MODULE)_qdma.o
ccflags-y += \
	-I$(src)/libslash/include \
	-I$(src)/libslash/include/slash/uapi \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma/qdma_access \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_cpm5_access \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_soft_access \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_cpm4_access \
	-I$(src)/$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_soft_access


LIBQDMA_OBJS := \
	$(QDMA_DRV_ROOT)/libqdma/qdma_mbox.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_intr.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_st_c2h.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_thread.o \
	$(QDMA_DRV_ROOT)/libqdma/libqdma_export.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_context.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_sriov.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_platform.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_descq.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_regs.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_debugfs.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_debugfs_dev.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_debugfs_queue.o \
	$(QDMA_DRV_ROOT)/libqdma/libqdma_config.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_device.o \
	$(QDMA_DRV_ROOT)/libqdma/xdev.o \
	$(QDMA_DRV_ROOT)/libqdma/thread.o

QDMA_ACCESS_OBJS := \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_mbox_protocol.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_list.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_access_common.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_resource_mgmt.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_cpm4_access/qdma_cpm4_access.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_cpm4_access/qdma_cpm4_reg_dump.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/qdma_soft_access/qdma_soft_access.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_soft_access/eqdma_soft_access.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_soft_access/eqdma_soft_reg_dump.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_cpm5_access/eqdma_cpm5_access.o \
	$(QDMA_DRV_ROOT)/libqdma/qdma_access/eqdma_cpm5_access/eqdma_cpm5_reg_dump.o

$(MODULE)-objs += $(LIBQDMA_OBJS) $(QDMA_ACCESS_OBJS)


all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo install -d -m 755 /lib/modules/$(shell uname -r)/extra
	sudo install -m 644 $(MODULE).ko /lib/modules/$(shell uname -r)/extra
	sudo depmod -a

.PHONY: all clean install
