..
   comment:: SPDX-License-Identifier: MIT
   comment:: Copyright (C) 2025 Advanced Micro Devices, Inc

###############
Platform Setup
###############

This tutorial walks a system administrator through installing the SLASH
stack on a machine with an AMD Alveo V80 board, from kernel module to
running ``v80-smi list``.

Prerequisites
=============

**Hardware:**

- AMD Alveo V80 board installed in a PCIe Gen5 x8 (or wider) slot.

**Software:**

- Linux (Ubuntu 22.04 recommended).
- Kernel headers for the running kernel.
- CMake 3.20+, GCC 9+ (C++17), a C17 compiler.
- pkg-config.

Install system dependencies (Debian/Ubuntu):

.. code-block:: bash

   sudo apt install cmake pkg-config \
     libxml2-dev libzmq3-dev libjsoncpp-dev zlib1g-dev \
     libsystemd-dev libinih-dev \
     linux-headers-$(uname -r)

Install the Kernel Module
=========================

.. code-block:: bash

   cd driver
   make
   sudo insmod slash.ko

Verify the module loaded:

.. code-block:: bash

   dmesg | grep slash

You should see messages for each V80 PCI function discovered.

Optional module parameters:

- ``qdma_num_threads=N`` — number of libqdma worker threads (default: 8).
- ``qdma_debugfs_path=/sys/kernel/debug`` — enable QDMA debugfs
  diagnostics.

To load the module automatically on boot:

.. code-block:: bash

   sudo cp slash.ko /lib/modules/$(uname -r)/extra/
   sudo depmod
   echo slash | sudo tee /etc/modules-load.d/slash.conf

Verify PCIe Device Visibility
==============================

Each V80 board exposes three PCI functions:

.. list-table::
   :header-rows: 1
   :widths: 15 20 25 40

   * - Function
     - Device ID
     - Driver
     - Purpose
   * - PF0
     - ``0x50B4``
     - ``ami``
     - AVED management interface
   * - PF1
     - ``0x50B5``
     - ``slash_qdma``
     - Queue-based DMA subsystem
   * - PF2
     - ``0x50B6``
     - ``slash_ctl``
     - BAR MMIO access (register reads/writes)

Check that all three appear:

.. code-block:: bash

   lspci -d 10ee: -k

All three functions should show their respective driver in the output.

Build and Install the Software Stack
=====================================

Components must be built in dependency order. See
:doc:`/howto/build-from-source` for detailed build instructions.

.. code-block:: text

   1. Linux kernel module (slash)   ← already done above
   2. libslash
   3. vrtd  (depends on libslash)
   4. VRT   (depends on vrtd)
   5. v80-smi (depends on VRT)

For each component:

.. code-block:: bash

   mkdir build && cd build
   cmake ..
   make
   sudo make install

Alternatively, building VRT will automatically pull in vrtd as a
subdirectory if it is not installed.

Start the vrtd Daemon
=====================

The vrtd daemon multiplexes access to V80 devices and enforces permissions.

Manual start:

.. code-block:: bash

   sudo vrtd

For production, enable the systemd service:

.. code-block:: bash

   sudo systemctl enable --now vrtd

Verify the daemon is reachable:

.. code-block:: bash

   v80-smi list

Each board should show all four readiness checks passing (PF0, PF1, PF2,
VRTD).

Validate the Board
==================

Run the built-in memory integrity and bandwidth test:

.. code-block:: bash

   v80-smi validate -d <BDF>

This tests both HBM and DDR subsystems. A passing result confirms the
hardware, drivers, and daemon are all working correctly.

User Access
===========

By default, only ``root`` and members of the ``vrtadmin`` group have full
device access. To grant a user access:

.. code-block:: bash

   sudo usermod -aG vrtadmin <username>

For fine-grained permission control (per-device, per-operation), edit
``vrtd.conf``. See :doc:`/reference/vrtd/configuration` for the full
configuration reference.

Next Steps
==========

- :doc:`device-management` — list, program, reset, and validate devices.
- :doc:`vrtd-configuration` — customise daemon permissions and roles.
- :doc:`/tutorials/user/getting-started` — run your first application.
