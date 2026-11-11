# RPU QDMA test ELF for R5_1 (bare metal, ATCM/BTCM, loaded over JTAG)
# Vitis ships the toolchain: <Vitis install>/gnu/aarch32/lin/gcc-arm-none-eabi/bin
CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
CFLAGS := -mcpu=cortex-r5 -mfloat-abi=soft -marm -Wall -Wextra -Werror -O2 -ffreestanding -nostdlib -g -Wl,--build-id=none
rpuq.elf: boot.S rpuq_main.c lscript.ld
	$(CC) $(CFLAGS) -T lscript.ld -o $@ boot.S rpuq_main.c -lgcc
	$(CROSS)size $@
clean:
	rm -f rpuq.elf
