
MODULE      := slash_driver

obj-m          := $(MODULE).o
$(MODULE)-objs := $(MODULE)_main.o $(MODULE)_ctldev.o $(MODULE)_pcie.o $(MODULE)_dmabuf.o
ccflags-y += -I$(PWD)/libslash_driver/include/slash_driver/uapi

KDIR        ?= /lib/modules/$(shell uname -r)/build
PWD         := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo install -d -m 755 /lib/modules/$(shell uname -r)/extra
	sudo install -m 644 $(MODULE).ko /lib/modules/$(shell uname -r)/extra
	sudo depmod -a

.PHONY: all clean install
