# ##################################################################################################
# Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
# This program is free software; you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation; version 2.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program; if
# not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
# ##################################################################################################

KMOD := pcie_hotplug

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
CC := g++

all: $(KMOD).ko

$(KMOD).ko: driver/$(KMOD).c
	$(MAKE) -C $(KDIR) M=$(PWD)/driver modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/driver clean


install: $(KMOD).ko
	cp driver/$(KMOD).ko /lib/modules/$(shell uname -r)/kernel/drivers/misc/
	depmod
	modprobe $(KMOD)
	chmod 666 /dev/$(KMOD)_* || echo "Warning: Failed to set permissions for dev"

uninstall:
	modprobe -r $(KMOD)
	rm -f /lib/modules/$(shell uname -r)/kernel/drivers/misc/$(KMOD).ko
	depmod

.PHONY: all clean install uninstall
