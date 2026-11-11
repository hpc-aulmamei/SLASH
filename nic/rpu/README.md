<!--
The MIT License (MIT)
Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
-->

# QDMA transfers started from the R5

R5_1 (the RPU core that runs RP1) rings the PIDX doorbells of QDMA queues set
up by the host driver. It reaches the doorbells and host memory through the
CPM AXI slave (slave bridge). The data itself does not go through the R5.

Tested so far:

- ST H2C: host memory to the RM's `qnoc_lb`.
- MM H2C and C2H: host memory to and from HBM/DDR.

## Pieces

### Static

`nic/shell/patch_rpuq.py`, run after `nic/shell/patch_shell.py` with
`NIC_BDF_BAR=1`. It adds a PL NMU (`base_logic/axi_noc_rpuq`) on a fourth
`rpu_sc` master, routed to `static_region/axi_noc_1/S01_INI` and from there
to `cips/NOC_CPM_PCIE_0`:

| R5 address | size | NoC address | |
|---|---|---|---|
| `0x9000_0000` | 128 MiB | `0xE800_0000` | slave bridge region 0, 64 windows of 2 MiB; window n uses BDF entry n (`0xFCEA_2420 + 0x20*n`) |
| `0x9800_0000` | 16 MiB | `0x7_1000_0000` | CPM5 DMA1 queue space (MULTQ) |

The service layer's route to the bridge (`0xE000_0000`, 256M) is not changed.
`rpuq_noc_locs.xdc` locks the NoC sites to the ones of the NIC static;
without it the NoC placer fails (`Place 30-1866`).

The patched static has a new interface UUID, so the RMs have to be rebuilt
against it.

### Driver

Queue flag `SLASH_QDMA_QPAIR_F_EXT_PRODUCER`, ioctls 0x57-0x5A, libqdma patch
`0004`. The driver adds and starts the queue but does not post descriptors or
poll it.

- `EXT_FILL` (ST H2C) and `EXT_FILL_MM` (MM, either direction) fill the ring
  once. Descriptor i points to page i of the buffer; for MM the card address
  is `dev_addr + i*4K`.
- `EXT_INFO` returns the doorbell index, function and the ring and writeback
  addresses.
- `EXT_DRAIN` waits for the hardware CIDX before the queue is stopped.

### Firmware

`rpuq.elf`, built from this directory, is a bare-metal test program for
R5_1. The host talks to it through a mailbox in DDR (R5 `0x3010_0000`, PF2
BAR4 + `0x0410_0000`). Commands:

- `PROBE`: read up to 8 addresses, reporting DECERR/SLVERR per address.
- `BDF`: write a BDF entry, read it back and clear it.
- `H2C`: write a PIDX doorbell in batches (any ST or MM queue, any
  direction). With flow control it reads CIDX from the status writeback
  through a BDF window.

The MPU maps `0x9000_0000-0x9FFF_FFFF` and the CPM registers as Device
memory; all accesses there are 32-bit (PG347).

### Host tool

`nic/sw/rpuq_ext.py`: `h2c`, `mm`, `rpu-probe`, `rpu-bdf`.

## Doorbell address

    0x9800_0000 + func*0x10000 + 0x18004 + 0x10*qidx    H2C PIDX
    0x9800_0000 + func*0x10000 + 0x18008 + 0x10*qidx    C2H PIDX

This is the same layout as the PF DMA BAR. All queues tested had qbase 0, so
it is not yet clear whether qidx is per function or the absolute queue ID.

## Running it (JTAG)

```bash
make CROSS=<Vitis>/gnu/aarch32/lin/gcc-arm-none-eabi/bin/arm-none-eabi-
# load the static and the RM partial over JTAG, then reload the driver
# (remove only the three PFs, not the root port):
for f in 0000:1b:00.2 0000:1b:00.1 0000:1b:00.0; do echo 1 | sudo tee /sys/bus/pci/devices/$f/remove; done
sudo rmmod slash; sudo insmod slash.ko; echo 1 | sudo tee /sys/bus/pci/rescan
sudo setpci -s 1b:00.2 COMMAND=0x02:0x02   # if PF2 is Mem- (mailbox reads 0xffffffff)
xsdb load_rpuq_elf.tcl
sudo ./rpuq_ext.py -d 0000:1b:00 rpu-probe --addr 0xFCEA0000 --addr 0x98000000
sudo ./rpuq_ext.py -d 0000:1b:00 rpu-bdf --entry 63
sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer rpu --flow --packets 1000000
sudo ./rpuq_ext.py -d 0000:1b:00 mm --producer rpu --mem hbm
```

Notes:

- Don't reset R5_1 from xsdb (`rst -processor`). After it the PFs lost their
  BARs and the R5 path returned DECERR until the static was reloaded.
  `load_rpuq_elf.tcl` only stops, downloads and continues.
- If the hw_server also sees other Versal boards, the loader picks R5 #1
  under the `Versal xcv80` target.
- xsdb reads in `0x8000_0000-0x9FFF_FFFF` need the device target (`ta 1`)
  and `mrd -force`.

## Results

Gen3 x8 host, static and nictest RM loaded over JTAG, 2026-11-11:

- `rpu-bdf --entry 63`: PASS.
- ST H2C, 1M packets of 4 KiB with CIDX flow control: PASS, 57.4 Gb/s
  (57.5 Gb/s with the host ringing the doorbell).
- MM, 8 MiB each way, data compared: PASS for HBM and DDR, on MM channels
  0 and 1, about 1.3 ms per direction (same as with the host).

## TODO

- Let the R5 write the descriptors itself (now the host fills the ring).
- Test a queue with qbase or qidx other than 0.
- ST C2H from the R5.
- Move this into RP1 instead of a separate ELF.
