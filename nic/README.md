# NIC static shell for the Alveo V80

This directory turns the SLASH service shell into a static shell for a NIC.
The CPM5 QDMA streaming interfaces are carried over the NoC into the
service-layer partition at a 390 MHz DMA clock, two 512-bit NoC ports per
data direction. A test RM (`nictest`) and host tools check both paths
between card and host:

- QDMA streaming, host to card (H2C) and card to host (C2H), through the
  SLASH driver
- the CPM5 slave bridge (card to host memory, card as the master)

The driver fix that QDMA streaming needs is in `driver/slash_qdma.c` on this
branch; see "Driver" below.

| Directory | Contents |
|---|---|
| `shell/` | the static: `patch_shell.py` (patches a slashkit tree), the static-side RTL (`rtl/`), its simulations (`sim/`), build scripts. Details in `shell/README.md` |
| `patches/` | `slash-1.0.0-service-pcie-route.patch`, the NoC route patch the SLASH 1.0.0 service shell needs (the "m05" tree `patch_shell.py` expects) |
| `rtl/` | RM logic: `qnoc_lb` (QDMA streaming sink, loopback, generator), `host_probe` (AXI4 master to host memory through the slave bridge) |
| `tcl/` | RM build: `build_service_rm.sh`, `gen_service_layer.py` (renders SLASH's service-layer template and adds the RM and its NoC endpoints), `nictest_service_layer.tcl`, `host_probe.tcl`, timing and partial helpers |
| `sim/` | xsim tests: `run_tb_nictest_lb.sh` (qnoc_lb through the static adapter and a NoC model), `run_tb_host_probe.sh` |
| `sw/` | host tools: `qnoc_lb.py` (streaming tests, QDMA register dump), `host_probe.py` (slave-bridge tests) |

## Build

All builds use Vivado 2025.1. The static takes most of a day and needs
about 100 GB of memory.

1. **Static.** Install the SLASH 1.0.0 slashkit and apply
   `patches/slash-1.0.0-service-pcie-route.patch` and the `versal_sdt` fix
   in `aved/plm/build-plm.sh` to get the m05 tree. Then:
   ```
   NIC_DMA_MHZ=390 shell/patch_shell.py <workspace>/shell-nic/pylib/slashkit
   NIC_WORK=<workspace> shell/run_nic_shell.sh 8
   ```
   `NIC_BDF_BAR=0` leaves out the PF1 BAR2 window onto the BDF table.
   The script runs `slashkit install` inside a memory-capped systemd scope.
   The scope's watchdog stops the build if MemAvailable drops below 30 GB.
   `resume_nic_shell.sh` finishes a run whose Vivado died or whose timing
   failed. `rerun_nic_impl.sh` reruns implementation only.
2. **RM.**
   ```
   STATIC_SHELL=<slashkit>/resources/static_shell \
   FULL_SHELL_DCP=<shell build>/slash.runs/impl_1/top_wrapper_routed.dcp \
       tcl/build_service_rm.sh <build dir> 12
   tcl/check_rm.sh <build dir>
   ```
   `SLASH` defaults to this repository, which supplies the service-layer
   template and `service_layer_build.tcl`. The partial must be written from
   the full routed design (`FULL_SHELL_DCP`, `full_partial.sh`). Partials from
   the abstract-shell run hang the PLM on this shell.

## Load and test

Test images go over JTAG, not flash. Flash partition 1 is not touched.

- **Static:** `/usr/share/v80-smi/versal_flash_pdi.tcl` with `PDI_PATH` set
  to the static PDI. It forces JTAG boot mode and resets the PMC; a plain
  `device program` is not enough.
- **RM:** `xsdb` `device program` of the partial.
- **Before stopping VRT:** stop `vrtd.socket` first. Stopping `vrtd.service`
  on its own has hung the test host several times.
- **Host:** the SLASH driver of this branch.

```
sudo sw/qnoc_lb.py -d 0000:1b:00 status
sudo sw/qnoc_lb.py -d 0000:1b:00 loop --bytes 16M          # H2C -> RM -> C2H, data compared
sudo sw/qnoc_lb.py -d 0000:1b:00 gen  --count 4096 --pkt 4096
sudo sw/qnoc_lb.py -d 0000:1b:00 h2c  --bytes 64M
sudo sw/qnoc_lb.py -d 0000:1b:00 regs                      # QDMA counters and errors, read only

sudo sysctl vm.nr_hugepages=256
sudo sw/host_probe.py -d 0000:1b:00 session --windows 64   # slave bridge, 64 x 2 MiB windows
sudo sysctl vm.nr_hugepages=0
```

`host_probe.py session` also programs the slave-bridge BDF table
(CPM5_DMA1_CSR `0xFCEA_2420`, `0x20` per entry). The QDMA driver has no code
for it, and offset `0x2420` of PF1 BAR0 is the PF mailbox, not this table.

- With `NIC_BDF_BAR=1` (default), PF1 BAR2 is a 16 KiB window onto
  `0xFCEA_0000` and the session writes the entries through it.
- With `NIC_BDF_BAR=0`, it writes xsdb scripts to run over JTAG instead.

`--bdf auto|host|jtag` picks the method. `host_probe.py bdf` reads the
entries and `bdf --clear` zeroes them. Both first read CSR offset 0
through PF1 BAR2. CPM5_DMA1_CSR starts with a memory-mapped copy of PF0's
PCIe config header, so this must return PF0's vendor/device ID
(`0x50b410ee`). PF1 BAR0 is the QDMA register block (`0x1fd3xxxx` at
offset 0), not this one.

PF1 BAR2 is 32-bit and non-prefetchable. The host firmware sizes the bridge
windows for the image the card boots from flash, which has no such BAR.
After a JTAG load, `/sys/bus/pci/rescan` then reports `BAR 2: no space`,
because PF2's two 128 MiB BARs fill the switch's non-prefetchable window.
Removing the switch above the card and rescanning the root port's
secondary bus re-sized the windows once on arhatlas00:
```
echo 1 | sudo tee /sys/bus/pci/devices/0000:19:00.0/remove
echo 1 | sudo tee /sys/class/pci_bus/0000:19/rescan
```
On 2026-11-09 it did not, and a warm reboot did (the JTAG-loaded static
survives it). A 64-bit prefetchable BAR would fit in the existing
prefetchable window without either step.

## Results

On a Gen3 x8 host (63 Gb/s raw, about 57.6 Gb/s of 256 B payload), on
2026-11-06/07:

| Test | Result |
|---|---|
| `h2c` 64 MiB | PASS, 57.4 Gb/s at the card |
| `gen` 4096 x 4 KiB | PASS, 54.5 Gb/s |
| `loop` 16 MiB (both directions at once, ring wraps past 2048) | PASS, 51.2 Gb/s each way |
| `loop`/`gen`, 64 KiB to 1 MiB, repeated on fresh queues in one driver load | PASS |
| slave bridge, 64 windows, 128 MiB, read and write, window guards | PASS; read 17.3 Gb/s, write 8.4 Gb/s, about 1 us read latency |
| BDF table written and cleared from the host through PF1 BAR2 (`NIC_BDF_BAR=1`, 2026-11-09) | PASS, same slave-bridge results |

All of these are limited by the PCIe link, not by the NoC or the RM. The RM
runs at 250 MHz (128 Gb/s per direction) and the static's DMA clock at
390 MHz (200 Gb/s per direction). The slave-bridge rates do not change with
burst length or outstanding count. PG347 describes the slave bridge as a
control and status path.

## Driver

The stock driver adds ST C2H queues with:

- **No completion trigger.** libqdma never sees the completion PIDX, so C2H
  transfers time out.
- **No prefetch and no fetch credits.** The CPM5 fetches every posted
  descriptor ahead of the packets. Stopping the queue frees the buffers but
  leaves those descriptors in the hardware, and the next queue's packets are
  written into freed pages.

With no IOMMU on the host, that can corrupt any memory. This branch sets
`TRIG_MODE_ANY`, `pfetch_en` and `fetch_credit` on ST C2H queues.

## Open

- **PF1 BAR2 after a JTAG load.** It needs the switch rescan or a warm
  reboot (see above); a 64-bit prefetchable BAR would avoid that.
- **`ERR_FAB`.** `QDMA_GLBL_ERR_STAT` bit 17 (`FAB_ERR_LOG` `0x53`) is set
  once per run that uses H2C queues. It is not decoded yet and has no
  visible effect.
- **Rates above Gen3.** The 390 MHz DMA clock and two NoC ports per
  direction need a Gen5 host to be measured.
