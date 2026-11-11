# RPU-driven QDMA on the V80 (QDMA mastering over the CPM slave bridge)

The V80's R5_1 (the RPU core that runs RP1) starts QDMA transfers on its own by writing the PIDX doorbells of queues that the host driver has set up. The R5 reaches the doorbells and host memory through the CPM AXI slave (the "slave bridge"). Payload never passes through the R5:

- **Stream H2C:** host memory → the RM's `qnoc_lb`, the same path as the NIC static.
- **MM H2C/C2H:** host memory ↔ HBM/DDR, the same path that `vrt::Buffer::sync` uses.

## What it needs

**Static: `nic/shell/patch_rpuq.py`.** It runs after `nic/shell/patch_shell.py` with `NIC_BDF_BAR=1`. It adds one PL NMU (`base_logic/axi_noc_rpuq`), fed by a fourth `rpu_sc` master, that goes to `static_region/axi_noc_1/S01_INI` and on to `cips/NOC_CPM_PCIE_0`:

| R5 address | size | NoC remap to | what |
|---|---|---|---|
| `0x9000_0000` | 128 MiB | `0xE800_0000` | slave-bridge region 0: 64 × 2 MiB windows; window *n* translates through BDF entry *n* (`CPM5_DMA1_CSR 0xFCEA_2420 + 0x20·n`) |
| `0x9800_0000` | 16 MiB | `0x7_1000_0000` | CPM5 DMA1 queue space ("MULTQ" decode) |

The service layer's own route to the bridge (`noc_virt_4`, `{0xE000_0000 256M}`) is unchanged. Neither the QDMA mailbox IP nor `dma1_mgmt` is used, and `dma1_usr_irq` stays with the RM.

`rpuq_noc_locs.xdc` pins the PL NMU/NSU sites to the NIC static's known-good NoC solution. Without the locks, NoC placement failed (`Place 30-1866` on `qnoc_rm_nsu/M02_AXIS_nsu`).

A static with this patch has a new interface UUID, so its RMs must be built against it.

**Driver: `SLASH_QDMA_QPAIR_F_EXT_PRODUCER` (ioctls 0x57–0x5A, libqdma patch `0004`).**
- The driver adds and starts the queue and programs the contexts. It never posts to the queue or polls it: there is no completion thread and no initial PIDX.
- `EXT_FILL` (ST H2C) and `EXT_FILL_MM` (MM H2C or C2H) fill the whole ring once:
  - ST: descriptor *i* = one packet in buffer page *i*.
  - MM: descriptor *i* = buffer page *i* ↔ `dev_addr + i·4 KiB`.
- `EXT_INFO` exports the doorbell index, function, ring and status-writeback addresses.
- `EXT_DRAIN` waits for the hardware CIDX before STOP.
- The queue pair keeps a reference on the buffer until it is deleted.

**Firmware: `rpuq.elf` in this directory, a bare-metal R5_1 test ELF.**
- The host drives it through a mailbox in shared DDR: R5 `0x3010_0000` = PF2 BAR4 + `0x0410_0000`.
- Operations:
  - `PROBE`: up to 8 reads. A data abort is recorded per address as DECERR or SLVERR, and the probe carries on.
  - `BDF`: write one BDF entry, read it back, clear it.
  - `H2C`: advance a queue's PIDX doorbell in batches. It works for any ST or MM queue, H2C or C2H. Optionally it reads CIDX from the status writeback through a BDF window, for flow control beyond one ring.
- MPU: `0x9000_0000`–`0x9FFF_FFFF` and `0xFC00_0000` (16 MiB) are Device, execute-never, with 32-bit accesses only (PG347 slave-bridge register rules).

**Tool: `nic/sw/rpuq_ext.py`.** Subcommands `h2c`, `mm`, `rpu-probe` and `rpu-bdf`; see its docstring.

## Doorbell addresses from the R5

`0x9800_0000 + func·0x1_0000 + 0x18004 + 0x10·qidx` for the H2C PIDX, and `+ 0x18008` for the C2H PIDX. This is the PF BAR layout at 64 KiB per function. The R5 read the host's PF1 BAR0 doorbell value back there, and doorbells written there moved data on queues with `qidx` 0 and 1.

The queues tested so far all had `qbase` 0, so a function-relative `qidx` and an absolute queue ID can't be told apart yet. CSR space (for example the ident at offset 0) reads 0 through this window.

## Bring-up (JTAG only)

```bash
make CROSS=<Vitis>/gnu/aarch32/lin/gcc-arm-none-eabi/bin/arm-none-eabi-
# static, then RM partial, over JTAG; then the driver with only the three PFs removed:
for f in 0000:1b:00.2 0000:1b:00.1 0000:1b:00.0; do echo 1 | sudo tee /sys/bus/pci/devices/$f/remove; done
sudo rmmod slash; sudo insmod slash.ko; echo 1 | sudo tee /sys/bus/pci/rescan
sudo setpci -s 1b:00.2 COMMAND=0x02:0x02      # only if PF2 shows Mem- (mailbox reads 0xffffffff)
xsdb load_rpuq_elf.tcl                         # R5_1 under "Versal xcv80": stop, dow, con
sudo ./rpuq_ext.py -d 0000:1b:00 rpu-probe --addr 0xFCEA0000 --addr 0x98000000
sudo ./rpuq_ext.py -d 0000:1b:00 rpu-bdf --entry 63
sudo ./rpuq_ext.py -d 0000:1b:00 h2c --producer rpu --flow --packets 1000000
sudo ./rpuq_ext.py -d 0000:1b:00 mm --producer rpu --mem hbm
```

Pitfalls:

- **Do not use `rst -processor` on R5_1.** It makes the PLM activate the default subsystem first. Afterwards the PFs had lost their BARs, and every R5 access through the new NoC path returned DECERR until the static was loaded again. `load_rpuq_elf.tcl` loads without a reset, and the ELF masks interrupts and resets its MPU, caches and vectors itself.
- **Remove and rescan only the three PFs.** Removing the upstream root port (`0000:19:00.0`) on the test host could not be undone without a reboot.
- **Pick the target explicitly when the hw_server sees other Versal boards.** The loader selects R5 #1 under the single `Versal xcv80` target.
- **xsdb memory reads in `0x8000_0000`–`0x9FFF_FFFF`** need the device target (`ta 1`) and `mrd -force`; the PMC target cannot read memory.

## Results (2026-11-11, Gen3 x8 host, JTAG-loaded static and nictest RM)

- Static timing met (WNS 0.000 ns); nictest RM against it WNS +0.003 ns.
- `rpu-probe`: the CPM registers are reachable from the R5. Bridge window 0 with an empty BDF entry answers SLVERR, which the CPM logs as BDF error type 1 (incomplete entry).
- `rpu-bdf --entry 63`: PASS.
- **ST H2C, R5 producer:**
  - 2047 packets, one shot: PASS.
  - 1,000,000 × 4 KiB with CIDX flow control through BDF entry 63: PASS, 57.38 Gb/s on the card, against 57.51 Gb/s with the host as producer (the Gen3 x8 limit). The R5 made 15,626 doorbell writes and 64,408 writeback polls; there were 0 bad or error packets.
- **MM, R5 producer:** host buffer → card memory → second host buffer, 8 MiB (2047 × 4 KiB) each way, byte-identical with a fresh pattern each run:

  | memory | card address | MM channel | result |
  |---|---|---|---|
  | HBM | `0x41_0000_0000` | 0 | PASS |
  | DDR | `0x601_0000_0000` | 0 | PASS |
  | HBM | `0x45_0000_0000` | 1 | PASS |

  Each direction took about 1.3 ms, the same as the host-driven baseline.

## Open points

- The R5 cannot yet pick card addresses or lengths per transfer, because the host pre-fills the rings. Doing that means the R5 writes MM descriptors into the ring through a BDF window, from a page table of the host buffer, and reads back the last one before the doorbell.
- Telling 64 KiB per function apart from an absolute queue ID needs a queue with `qbase` or `qidx` other than 0.
- Stream C2H with the R5 as producer is not tested.
- `GLBL_ERR_STAT 0x20000` and `FAB_ERR_LOG 0x53` stay constant across runs; they still need comparing with a run on the plain NIC static.
- Long term, this belongs in RP1 (`QDMA_PIDX` / `QDMA_WAIT_CIDX` operations) rather than in a separate test ELF.
