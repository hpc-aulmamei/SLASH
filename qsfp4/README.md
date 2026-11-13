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

# Four QSFP cages of the V80 at 200G: Ethernet loopback test

A service-layer RM that drives all four QSFP56 cages of an Alveo V80 at
200GAUI-4 (and 100CAUI-4, switched at runtime) with plain Ethernet frames, a
generator and a checker per cage, and the host tools to run it. No RDMA or
RoCE. A test is a set of paths: cages joined by a cable, or a cage looped back
on itself in its GT (near-end PMA loopback, no module needed).

This branch is SLASH `dev` plus:

| Where | Change |
|---|---|
| `submodules/Versal-DCMAC` | the hpc-aulmamei fork, branch `dynamic-rate`: both 200G ports of a DCMAC (dual mode), reset sequencer fixes, runtime 200G / 100G |
| service shell, `service_layer.tcl` | both DCMACs in dual mode in the default service layer, so `qsfp1_4x` and `qsfp3_4x` are driven and stay on the static / RM boundary. Stock `dev` leaves them undriven: they drop out of `top_wrapper` and no RM can use QSFP1 or QSFP3 |
| service shell, `opt.post.tcl` | LOCs of the four GT quads; hard pblocks around each DCMAC's fabric |
| service shell, `top.tcl` | NoC route from the CPM PCIe ports to the service layer's `S_AXILITE_INI` (PF2 BAR2) |
| `qsfp4/` | the test RM (RTL, block design, build), the JTAG load, the host tools |

## Ports

| Cage (bracket) | SLASH | GT quad | DCMAC, port | Tester at PF2 BAR2 |
|---|---|---|---|---|
| 1 | QSFP0 | `GTM_QUAD_X1Y7` | DCMAC0 (`DCMAC_X1Y1`), 0 | `0x0400_0000` |
| 2 | QSFP1 | `GTM_QUAD_X1Y8` | DCMAC0, 2 | `0x0401_0000` |
| 3 | QSFP2 | `GTM_QUAD_X0Y10` | DCMAC1 (`DCMAC_X0Y2`), 2 | `0x0402_0000` |
| 4 | QSFP3 | `GTM_QUAD_X0Y9` | DCMAC1, 0 | `0x0403_0000` |

The tools use the SLASH numbers (QSFPn). DCMAC1's port order is the one the
DCMAC IP gives `DCMAC_X0Y2` in dual mode (QSFP3 on port 0). DCMAC registers:
`0x0200_0000` (DCMAC0) and `0x0300_0000` (DCMAC1), a port's block at
`0x1000 * (port + 1)`. NoC lane test: `0x0600_0000`.

Both ports of a DCMAC share a clock wizard, a reset sequencer and the rate: the
DCMAC IP generates the GT lane mapping only for configurations that are all
PAM4 or all NRZ, so `speed` switches a DCMAC (two cages) at a time.

## Build

Vivado and Vitis 2025.1, a host with at least 128 GB of memory, and Python 3
with `jinja2` (the linker's only dependency). The scripts run this checkout's
linker (`PYTHONPATH=linker`), not an installed `slashkit`.
The SMBus IP is not in the repository: download it from AMD and copy it into
`linker/slashkit/resources/base/common/iprepo/smbus*/`, with its license and a
DCMAC license in your Vivado license setup
(`docs/howto/install-from-packages.rst`). `build_shell.sh` builds the HLS IPs of
that IP repository itself.

```sh
git clone -b qsfp4 https://github.com/hpc-aulmamei/SLASH.git && cd SLASH
git submodule update --init submodules/Versal-DCMAC
source <Vivado 2025.1>/settings64.sh; source <Vitis 2025.1>/settings64.sh

qsfp4/build_shell.sh ../qsfp4-shell 8      # about 14 h; the build dir must not exist
qsfp4/build_rm.sh ../qsfp4-rm 8            # about 5 h
```

`build_shell.sh` installs the shell into `linker/slashkit/resources/static_shell`
(gitignored): the abstract shell and base BD that the RM build links against,
`amd_v80_gen5x8_25.1_nofpt.pdi` (the JTAG boot image) and
`qsfp4_top_wrapper_routed.dcp` (the routed full design). `slashkit install`
deletes its build directory first, so a failed run cannot be resumed with it.

`build_rm.sh` builds the RM against that shell, writes the partial from the full
design (routed shell with the routed RM in its service-layer partition; a partial
written from the abstract-shell run hung the PLM on load with the SLASH 1.0.0
shell) and checks it with `pr_verify`. Output: `<build dir>/images/qsfp4_partial.pdi`.
`QSFP_DYNAMIC=0` builds a 200G-only RM.

## Load

JTAG only; flash is not touched, and a power cycle brings back the card's
flash image. On the host with the card, with `xsdb` on PATH:

```sh
qsfp4/load.sh -d 0000:1b:00 linker/slashkit/resources/static_shell/amd_v80_gen5x8_25.1_nofpt.pdi \
    ../qsfp4-rm/images/qsfp4_partial.pdi
qsfp4/load.sh -d 0000:1b:00 --rm-only ../qsfp4-rm/images/qsfp4_partial.pdi   # shell already loaded
```

`load.sh` stops `vrtd`, removes the card's PCIe functions, boots the shell
(`smi/resources/versal_flash_pdi.tcl`), rescans, removes them again, programs
the partial (`tcl/load_partial.tcl`) and rescans. It only removes the card's own
functions, never the root port above it: after a root-port remove the BAR
windows cannot be allocated again until a reboot. With several V80s on the
JTAG chain set `V80_CABLE_SERIAL` or `V80_TARGET_ID`.

The shell enables PF3 (DDR / HBM BARs, 2 × 32 GB) as `dev` does. A host whose
bridge window was sized at boot for a shell without PF3 cannot place those BARs
after a JTAG load; PF0 to PF2 are unaffected and the test only uses PF2 BAR2.

## Test

```sh
qsfp4/test.sh -d 0000:1b:00                              # all four cages in GT loopback
qsfp4/test.sh -d 0000:1b:00 --pair 0-2 --pair 1-3        # two cables, cage 1-3 and 2-4
qsfp4/test.sh -d 0000:1b:00 --pair 1-3 --seconds 600     # one cable, QSFP0 and QSFP2 in loopback
```

`test.sh` checks that the four testers answer, runs 200G traffic on every path,
switches both DCMACs to 100G (RS(528) FEC) for a frame-length sweep (60 to
8996 bytes) and a timed run, and comes back to 200G. It stops at the first
failing step; the log is `qsfp4_test.log`. A pass at 200G is about 198 Gb/s per
direction per cage at 9000-byte frames, with no lost, reordered or corrupted
frame and no uncorrected FEC codeword.

The tools in `sw/` run with `sudo` and map PF2 BAR2 through sysfs; they need no
driver:

| Tool | |
|---|---|
| `qsfp_loop.py` | everything: `id`, `phy`, `speed 100\|200`, `up`, `run`, `lengths`, `dcmac`, `fault`, `restart`, `stop` (see its `--help`) |
| `diag100.py` | 100G diagnosis: which reset clears a bad port, repeated 200G / 100G switches |
| `nic100.py`, `nic100_test.sh`, `nic_pktgen.sh` | one cage against an external 100G NIC (e.g. a ConnectX-5), both directions |
| `clkppm.py` | QSFP reference clock offset against the host clock |
| `phy_pulse.py` | pulse one GT reset of one port and trace the PHY status |

Optical modules: the V80 leaves LPMode to the pin. If a module does not light
up, set high power in the module (SFF-8636 byte 93), as `nic100_test.sh` does
with `ami_tool module_byte_wr`.

## Runtime rate

`qsfp_loop.py speed 100 [--dcmac D]` and `speed 200`: PHY_CTRL[8] of the
DCMAC's port-0 tester selects GT line rate 1 (25.78125G NRZ), DCMAC
configuration 1 (100CAUI-4 on both ports) and two TX segments a cycle; the tool
then writes the DCMAC port registers (TX_MODE / RX_MODE, FEC) and resets the
ports. 100G uses RS(528) FEC by default, with TX_MODE bit 22 (four-lane PMD)
set: a ConnectX-5 does not lock without it. At 100G the RX segments 2 and 3 of
each port are masked (`rtl/dcmac_rx_seg_mask.v`): after a 200G frame the DCMAC
can leave stale data on them, which corrupted 100G frames.

## Register map of eth_test

32-bit registers, whole-word writes, no read side effects (the host path turns
every BAR2 read into two). 64-bit values: LO at `a`, HI at `a + 4`, read HI, LO,
HI. Frame format: `rtl/eth_pattern.vh`.

| Offset | Name | Access | Description |
|---|---|---|---|
| `0x000` | ID | R | `0x45544854` ("ETHT") |
| `0x004` | VERSION | R | |
| `0x008` | SCRATCH | RW | |
| `0x00C` | BUILD | R | |
| `0x010` | CYCLES | R | 64 bit, 230 MHz |
| `0x018` | PORT | R | SLASH port number |
| `0x020` | PHY_CTRL | RW | [2:0] GT loopback (0 off, 1 near-end PCS, 2 near-end PMA, 4 far-end PMA, 6 far-end PCS), [3] RX CDR hold, [4] GT RX datapath reset, [5] GT RX PLL and datapath reset, [6] GT TX datapath reset, [7] hold the DCMAC's reset sequencer (reruns on release; either port of the DCMAC), [8] 100G (port-0 tester of the DCMAC) |
| `0x024` | PHY_STATUS | R | [0] GT TX reset done, [1] GT RX reset done, [2] GT power good, [3] DCMAC clock wizard locked, [4] DCMAC TX core reset, [5] RX core reset, [6] RX serdes reset of this port, [7] AXIS out of reset, [8] sequencer held, [9] 100G |
| `0x028` | RX_DROPS | R | behind the DCMAC, free running: [11:0] bad FCS, [15:12] overflow |
| `0x040` | G_CTRL | W: [0] start [1] stop [2] clear; R: [0] running | generator |
| `0x044` | LEN_MIN | RW | bytes without FCS, at least 60 |
| `0x048` | LEN_MAX | RW | |
| `0x04C` | LEN_MASK | RW | length = LEN_MIN + (random & mask), at most LEN_MAX; 0: fixed |
| `0x050` | GAP | RW | idle cycles between frames |
| `0x054` | ETYPE | RW | default `0x88B5` |
| `0x058` | COUNT | RW | 64 bit, frames, 0: until stop |
| `0x060` | G_DST | RW | 48 bit |
| `0x068` | G_SRC | RW | 48 bit |
| `0x070` | SEED | RW | 64 bit, shared by generator and checker |
| `0x080` | TX_FRAMES | R | 64 bit |
| `0x088` | TX_BYTES | R | 64 bit, without FCS |
| `0x100` | C_CTRL | W: [0] clear | checker |
| `0x108` | C_DST | RW | 48 bit, expected |
| `0x110` | C_SRC | RW | 48 bit, expected |
| `0x118` | C_ID | RW | expected sending port |
| `0x120` | RX_FRAMES | R | 64 bit |
| `0x128` | RX_BYTES | R | 64 bit |
| `0x130` | LOST | R | 64 bit, sequence numbers skipped |
| `0x138` | REORDER | R | |
| `0x13C` | HDR_ERR | R | frames with a wrong MAC, EtherType or port id |
| `0x140` | LEN_ERR | R | beats whose tkeep / tlast disagree with the length field |
| `0x144` | PAY_ERR | R | payload beats with a mismatch |
| `0x148` | LAST_ID | R | sending port of the last frame: shows which cage this one hears |
| `0x14C` | LAST_SRC | R | 48 bit |
| `0x154` | FIRST | R | first error: [31] valid [23:20] lane [18:16] kind (0 payload, 1 header, 2 length) [15:0] beat |
| `0x158` | FIRST_SEQ | R | 64 bit |
| `0x160` | FIRST_EXP | R | 64 bit, the lane |
| `0x168` | FIRST_GOT | R | 64 bit |

FEC counters in the DCMAC (PG369): `0x0E48` processed, `0x0E50` corrected,
`0x0E58` uncorrected codewords, plus `0x18` for the `_BH` halves, in each port
block, per tick. `qsfp_loop.py` sums them over the ticks of a run.

## Layout

| Path | |
|---|---|
| `build_shell.sh`, `build_rm.sh` | shell install and RM build |
| `load.sh`, `test.sh` | JTAG load and the four-cage test, on the host with the card |
| `rtl/` | `eth_test` (`eth_gen`, `eth_chk`), the DCMAC RX / TX datapaths at 1024 bits (`dcmac_rx_path`, `dcmac_tx_path`, `rx_fcs_filter`), `dcmac_phy_ctl` (GT and sequencer control), `dcmac_rx_seg_mask` (100G), NoC lane test |
| `tcl/` | RM block design (`gen_service_layer.py` renders SLASH's service-layer template and inserts `qsfp_service_layer.tcl`), Versal-DCMAC overrides (`dcmac_datapath.tcl`, `qsfp_dcmac.tcl`), placement (`opt_post.tcl.in`), partial from the full design, `pr_verify`, JTAG partial load |
| `sw/` | host tools |

## Limits

- The RMs that the SLASH linker generates do not drive `qsfp1_4x` / `qsfp3_4x`;
  on this shell they need the same treatment as `tcl/qsfp_service_layer.tcl`
  (ports recreated and connected) before they link.
- The flash image of this shell (`amd_v80_gen5x8_25.1.pdi`) is not used or tested here.
