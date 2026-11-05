# RDMA sideband protocol (V80 VRT <-> VPK180), version 1

A TCP connection beside the 200G RoCE link. The two ends use it to swap
queue-pair and memory parameters, start tests on each other and report
results. RDMA payload never goes over it.

## Roles

- One end is the **server**: it listens on a TCP port and accepts one
  connection. The other end is the **client**: it connects to that port.
- **Both ends must support both roles.**
  - VRT (V80 host, arhatlas00), as settings of `vrt::Rdma` before `connect()`:
    ```cpp
    auto rdma = device.getRdmaDevice();
    rdma.setLocalIp("10.0.0.1");                 // RoCE address of the V80
    rdma.setRemoteIp("10.0.0.3");                // optional: checked against the peer's
    rdma.setRole(vrt::Rdma::Role::Client);       // or Server (default)
    rdma.setRemoteHost("<vpk sideband ip>");     // client only
    rdma.setPort(18515);                 // default
    rdma.connect();
    ```
  - VPK180 (`roce_ctl`, lwIP on the PS Ethernet): console commands
    `sb listen <port>` and `sb connect <ip> <port>`.
- Default port: **18515** (the perftest port).
- The role only decides who connects. After the connection is up, the
  protocol is symmetric: either end may send any request.
- The **driver** is the end that runs the test plan. It is chosen
  independently of the role; by default it is VRT.
- The sideband address is the VPK's PS Ethernet address on the lab network.
  It is not the RoCE address (10.0.0.1 V80 / 10.0.0.3 VPK, on the 200G link).

## Framing

- ASCII lines ending in `\n`, at most 255 bytes each, read with any
  `sscanf`-style parser. A receiver must reassemble lines split across TCP
  segments and split segments that hold several lines.
- A line is `<VERB> [#<tag>] key=value key=value ...`.
  - Numbers are decimal or `0x` hex.
  - Unknown keys are ignored, so later versions can add keys.
  - Keys may come in any order.
- A request carries `#<tag>`, a decimal number unique per sender. Its answer
  echoes the tag: `OK #<tag> ...` or `ERR #<tag> msg=<text-without-spaces>`.
- `HELLO`, `QP`, `MR` and `READY` are announcements, with no tag and no
  answer.
- Requests may take long (a `RUN` of 400 MB). The receiver answers when the
  work is done. A sender keeps at most one request outstanding.

## Session

0. Before the TCP connection, each end brings its 200G link up and sets its
   own MAC and RoCE IP in the stack, so that it answers ARP as soon as the
   peer has its `HELLO`.

1. **Both ends send `HELLO`, `QP` and `MR`** without waiting for the other.
   - `HELLO v=1 name=<v80|vpk180> roce_ip=<dotted> qps=<n> mrs=<m>`
   - `QP i=<0..n-1> qpn=<local QPN> send_psn=<first PSN this end sends, 24 bit> rkey=0 udp=4791`
   - `MR i=<0..m-1> addr=<base address the peer puts in WRITE/READ> len=<bytes>`

   The QPs pair by index `i`: this end's QP `i` talks to the peer's QP `i`.
   Each end sets its stack to:
   - local QPN: own `qpn`;
   - remote QPN: the peer's `qpn`;
   - first PSN sent: own `send_psn`;
   - first PSN expected: the peer's `send_psn`.

   **Use fresh PSNs each session** (random 24-bit values), so a stale packet
   from an earlier run is caught.

2. Each end resolves the peer's `roce_ip` by ARP, connects its QPs, then
   sends **`READY`**. No RDMA before both ends have
   sent and received `READY`.
   - If `v` differs, or the link or ARP fails: send `ERR msg=...` (no tag)
     and close.

3. **Requests**, sent by the driver, or by either end:

   | Request | The receiver... | Answer |
   |---|---|---|
   | `FILL #t mr= off= len= seed=` | writes the pattern into its own MR `mr`, from byte `off`, `len` bytes | `OK #t` |
   | `CHECK #t mr= off= len= seed=` | compares that range of its own MR with the pattern | `OK #t errors=<words wrong> first=<byte offset of the first wrong word, or -1>` |
   | `RUN #t op=write\|read qp= count= size= laddr= raddr=` | posts `count` RDMA ops of `size` bytes on its queue pair of index `qp` (the `i` of `QP`, not a QPN): op `k` from/to local `laddr + k*size` and remote `raddr + k*size`, then waits for all completions | `OK #t ns=<elapsed> retrans=<delta> psn_drops=<delta>` |
   | `STATS #t` | reports its counters | `OK #t key=value ...` (free list: rx/tx frames, retrans, psn_drops, rx drops, ...) |
   | `BYE` | closes after the answer | `OK` (no tag) |

   - For `RUN`, `laddr` is in the receiver's own address space; `raddr` is
     an address the receiver puts on the wire, inside one of the
     requester's MRs.
   - On the V80, a local address can also be a stream address (bit 47) for
     user-region streams. The VPK never needs to know.

VRT in version 1 is always the driver. It answers a peer's `STATS` and
`BYE` whenever it reads the sideband (during its own requests), and answers
`FILL`, `CHECK` and `RUN` with `ERR #t msg=unsupported`. The VPK must serve
all requests.

## Pattern

For a range with seed `s`, the 32-bit little-endian word at byte offset
`4*k` from the start of the range is `(s + k) mod 2^32`.

The ranges of `FILL` / `CHECK` and the `count*size` span of a `RUN` are
contiguous. A misplaced, repeated or missing message therefore shows up as
wrong words. `len` and `size` are multiples of 4.

If the VPK's existing fill/check hardware uses a different pattern, the VPK
agent documents it and the spec adopts that one instead. Both ends must
produce identical words.

## Test plan of the end-to-end example (VRT as driver)

All four transfer directions, each checked by the end that receives the data:

1. **V80 WRITE.** VRT fills its DDR (host write through a `vrt::Buffer`),
   posts `count` WRITEs into VPK MR 0, waits, then sends `CHECK` to the VPK.
2. **V80 READ.** `FILL` to the VPK, then VRT posts READs into its DDR and
   checks them on the host.
3. **VPK WRITE.** `RUN op=write` to the VPK, aimed at V80 MR 0; VRT then
   checks its DDR.
4. **VPK READ.** VRT fills its DDR, sends `RUN op=read` to the VPK, then
   `CHECK` to the VPK.
5. `STATS` from both ends, then `BYE`.

The example prints one line per test: bytes, Gb/s, retransmissions, check
errors. It exits 0 only if every check has 0 errors.

Default sizes: 2000 x 4 KiB to start; `--count` / `--size` for 100000 x 4 KiB
and 2 x 200 MB. The V80 side's own rate is limited by host posting (one
register write per request, about 5 Gb/s from Python, more from C++); the
peer-driven tests show line rate.
