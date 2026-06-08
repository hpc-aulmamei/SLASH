..
   comment:: SPDX-License-Identifier: MIT
   comment:: Copyright (C) 2025 Advanced Micro Devices, Inc

#####################
Command Reference
#####################

``v80-smi`` is the command-line system management interface for AMD Alveo V80
boards. Running ``v80-smi`` with no subcommand prints usage help.

Device Addressing
=================

Several commands accept a ``-d/--device`` option that takes a **BDF**
(Bus:Device.Function) address. The following formats are supported:

.. list-table::
   :header-rows: 1
   :widths: 30 30

   * - Format
     - Example
   * - ``BB:DD`` (short)
     - ``03:00``
   * - ``BB:DD.F`` (short with function)
     - ``03:00.0``
   * - ``DDDD:BB:DD`` (domain:bus:device)
     - ``0000:03:00``
   * - ``DDDD:BB:DD.F`` (full)
     - ``0000:03:00.0``

Commands
========

version
-------

Print the v80-smi version and exit.

.. code-block:: text

   v80-smi version [-p|--plain]

.. option:: -p, --plain

   Print only the version in ``x.y.z`` format with no prefix. Useful for
   scripting.

list
----

Enumerate V80 boards visible on the system and report their readiness status.

.. code-block:: text

   v80-smi list [-j|--json] [-J|--pretty-json] [-l|--long] [-s|--sensors]

.. option:: -j, --json

   Output as compact JSON.

.. option:: -J, --pretty-json

   Output as indented JSON.

.. option:: -l, --long

   Include additional information (PCI IDs, driver status).

.. option:: -s, --sensors

   Include sensor readings (temperature, power). Requires the vrtd daemon to be
   running.

inspect
-------

Display metadata from a vrtbin file on disk without programming it onto a
device.

.. code-block:: text

   v80-smi inspect <vbin> [-j|--json] [-J|--pretty-json]

.. option:: vbin

   Path to the vrtbin file. **Required.**

.. option:: -j, --json

   Output as compact JSON.

.. option:: -J, --pretty-json

   Output as indented JSON.

query
-----

Display the metadata of the vrtbin currently loaded on a device.

.. code-block:: text

   v80-smi query -d <BDF> [-j|--json] [-J|--pretty-json]

.. option:: -d, --device <BDF>

   Board address. **Required.**

.. option:: -j, --json

   Output as compact JSON.

.. option:: -J, --pretty-json

   Output as indented JSON.

program
-------

Load a vrtbin file onto a device, programming the FPGA.

.. code-block:: text

   v80-smi program <vbin> -d <BDF>

.. option:: vbin

   Path to the vrtbin file. **Required.**

.. option:: -d, --device <BDF>

   Board address. **Required.**

reset
-----

Perform a hardware reset of a V80 board. This executes a full PCIe secondary
bus reset and rescan (hotplug) sequence.

.. code-block:: text

   v80-smi reset -d <BDF>

.. option:: -d, --device <BDF>

   Board address. **Required.**

validate
--------

Run memory integrity and bandwidth tests against a board's HBM and DDR
subsystems. For each memory path, bandwidth is reported as single-direction
C2H read, single-direction H2C write, and simultaneous bidirectional
throughput (read, write, and total). After the per-memory phases, a final
parallel phase drives HBM and DDR simultaneously with ``2 * N`` buffers for
single-direction tests and ``4 * N`` threads for bidirectional tests; this
phase is skipped when ``--ddr-only`` or ``--hbm-only`` is given.

.. code-block:: text

   v80-smi validate -d <BDF> [-j|--threads <N>] [-R|--no-reset] [--raw-transfer-test | --use-qdma-driver] [--ddr-only | --hbm-only]

Requirements by mode:

* Default mode uses VRTD buffers, requires a running VRTD daemon, and resets
  the board unless ``--no-reset`` is given.
* ``--raw-transfer-test`` bypasses VRTD for transfers and requires the SLASH
  QDMA driver device node for the board. It skips reset.
* ``--use-qdma-driver`` bypasses both VRTD and SLASH for transfers and requires
  the stock ``qdma-pf`` driver to be bound to the board's QDMA PF. This backend
  is built only when ``SMI_ENABLE_QDMA_DRIVER_BACKEND`` is enabled at CMake
  configure time.

.. option:: -d, --device <BDF>

   Board address. **Required.**

.. option:: -j, --threads <N>

   Number of parallel buffers/threads for the validation test (1–64, default 8).
   Each buffer is 512 MB (one HBM/DDR allocator region). The bidirectional HBM
   phase uses ``2 * N`` HBM regions, so values above 32 require ``--ddr-only``.
   The largest phase maps up to ``4 * N * 512 MB`` of host buffers when both
   HBM and DDR are enabled, or ``2 * N * 512 MB`` with ``--ddr-only`` or
   ``--hbm-only``; the command fails early if that exceeds currently available
   host memory.

.. option:: -R, --no-reset

   Skip the device reset step before running memory tests.

.. option:: --raw-transfer-test

   Use libslash raw QDMA transfers instead of VRTD buffers. This mode implies
   ``--no-reset`` and requires the SLASH QDMA driver device to be present.

.. option:: --use-qdma-driver

   Run the raw transfer test over the off-the-shelf Xilinx QDMA driver
   (``/dev/qdma<idx>-MM-<qid>``) instead of SLASH. smi provisions the queues
   itself: it raises the function's ``qmax`` via sysfs if needed, creates and
   starts bidirectional AXI-MM queue pairs over generic netlink (the same
   ``xnl_pf`` interface ``dma-ctl`` uses), then transfers over the per-queue
   char devices. Queue pairs are spread round-robin across the function's MM
   engine channels (``channel = qid % mm_channel_max``); the CPM5 QDMA on the
   V80 exposes two, so the test exercises both. This mode implies
   ``--no-reset`` and is mutually exclusive with ``--raw-transfer-test``. It
   requires the stock ``qdma-pf`` driver to be bound to the board's PF (it
   cannot be bound at the same time as the SLASH driver), and typically
   requires root to raise ``qmax`` and open the queue devices.

.. option:: --ddr-only

   Run only the DDR memory tests and skip the HBM phase. Mutually exclusive
   with ``--hbm-only``.

.. option:: --hbm-only

   Run only the HBM memory tests and skip the DDR phase. Mutually exclusive
   with ``--ddr-only``.

debug
-----

Low-level troubleshooting commands.

debug bar-poke
^^^^^^^^^^^^^^

Read or write BAR words.

.. code-block:: text

   v80-smi debug bar-poke -d <BDF> -b <BAR> (-r|--read | -w|--write) [-x|--hex] [-W|--word-size <N>] [-c|--count <N>] <address> [value]

.. option:: -d, --device <BDF>

   Board address. **Required.**

.. option:: -b, --bar <BAR>

   BAR number (0-5). **Required.**

.. option:: -r, --read

   Read mode.

.. option:: -w, --write

   Write mode.

.. option:: -x, --hex

   Print read output in hexadecimal.

.. option:: -W, --word-size <N>

   Access width in bytes: 1, 2, 4, or 8 (default 4).

.. option:: -c, --count <N>

   Number of words to read (default 1; must be 1 for write).

Rules:

- Exactly one of ``--read`` or ``--write`` must be provided.
- ``value`` is required for write and forbidden for read.
- ``address`` is a BAR-relative byte offset.

debug mem-poke
^^^^^^^^^^^^^^

Read or write device memory at a raw physical address. This bypasses the
allocator and requires raw-mem-access permission in vrtd.

.. code-block:: text

   v80-smi debug mem-poke -d <BDF> (-r|--read | -w|--write) [-x|--hex] [-W|--word-size <N>] [-c|--count <N>] <address> [value] [-f|--file <path>]

.. option:: -d, --device <BDF>

   Board address. **Required.**

.. option:: -r, --read

   Read mode.

.. option:: -w, --write

   Write mode.

.. option:: -x, --hex

   Hex mode.

   - Read-to-stdout: prints values in hexadecimal.
   - With ``--file``: treats file payload as hex text/hexdump format.

.. option:: -W, --word-size <N>

   Access width in bytes: 1, 2, 4, or 8 (default 4).

.. option:: -c, --count <N>

   Number of words to transfer (default 1).

.. option:: -f, --file <path>

   File mode transfer path.

   - In read mode: destination file.
   - In write mode: source file.

Rules:

- Exactly one of ``--read`` or ``--write`` must be provided.
- ``address`` is a device physical address.
- ``word-size`` must be 1, 2, 4, or 8.
- ``count`` must be greater than zero.
- Scalar mode (no ``--file``):
  - write requires ``value`` and forces ``count == 1``
  - read forbids ``value``
  - address must be aligned to word-size
- File mode (``--file`` present):
  - ``value`` is forbidden
  - transfer size is exactly ``word-size * count`` bytes
  - with ``--hex`` file is text hex/hexdump; without ``--hex`` file is raw binary

debug clockwiz
^^^^^^^^^^^^^^

Read or set clock rate for a device clock region using vrtd clock-op.

.. code-block:: text

   v80-smi debug clockwiz -d <BDF> (--get | --set <rate_hz>) [--region <user|service>] [-x|--hex]

.. option:: -d, --device <BDF>

   Board address. **Required.**

.. option:: --get

   Read current clock rate for selected region.

.. option:: --set <rate_hz>

   Set requested clock rate in Hz for selected region.

.. option:: --region <user|service>

   Clock region selector (default: ``user``).

.. option:: -x, --hex

   Print ``--get`` output in hexadecimal.

Rules:

- Exactly one of ``--get`` or ``--set`` must be provided.
- ``--set`` value is in Hz and must be greater than zero.
- ``--hex`` is valid only with ``--get``.
- ``--set`` prints requested and achieved frequencies.
