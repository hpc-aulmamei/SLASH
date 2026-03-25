..
   comment:: SPDX-License-Identifier: MIT
   comment:: Copyright (C) 2025 Advanced Micro Devices, Inc

####################
Build from Source
####################

This guide covers building all SLASH components from the repository.

Prerequisites
=============

- **CMake** 3.20 or later
- **C++ compiler** with C++17 support (GCC 9+, Clang 10+) — v80-smi requires
  C++20
- **C compiler** with C17 support
- **Linux kernel headers** (for the kernel module)
- **pkg-config**

Library dependencies:

- **libxml2** — XML parsing (vrtbin ``system_map.xml``)
- **ZeroMQ** (libzmq) — emulation/simulation IPC
- **JsonCpp** — JSON manifest and command handling
- **zlib** — vrtbin archive decompression
- **libsystemd** — vrtd daemon integration
- **inih** — INI configuration parsing (vrtd)

On Debian/Ubuntu:

.. code-block:: bash

   sudo apt install cmake pkg-config \
     libxml2-dev libzmq3-dev libjsoncpp-dev zlib1g-dev \
     libsystemd-dev libinih-dev \
     linux-headers-$(uname -r)

Build Order
===========

Components must be built in dependency order:

.. code-block:: text

   1. Linux kernel module (slash)
   2. libslash
   3. vrtd  (depends on libslash)
   4. VRT   (depends on vrtd)
   5. v80-smi (depends on VRT)

Alternatively, VRT can build vrtd as a CMake subdirectory automatically.

Linux Kernel Module
===================

.. code-block:: bash

   cd driver
   make
   sudo insmod slash.ko

Optional module parameters:

- ``qdma_num_threads=N`` — number of libqdma worker threads (default: 8).
- ``qdma_debugfs_path=/sys/kernel/debug`` — enable QDMA debugfs diagnostics.

vrtd (Daemon)
=============

.. code-block:: bash

   cd vrt/vrtd
   mkdir build && cd build
   cmake ..
   make

This produces:

- ``libvrtd`` — C wire-protocol client library.
- ``libvrtdpp`` — C++ RAII wrapper library.
- ``vrtd`` — the daemon executable.

Install:

.. code-block:: bash

   sudo make install

VRT (Runtime Library)
=====================

.. code-block:: bash

   cd vrt
   mkdir build && cd build
   cmake ..
   make

If vrtd is not installed system-wide, VRT will build it as a subdirectory
automatically. To force this behaviour:

.. code-block:: bash

   cmake .. -DFETCHCONTENT_FULLY_DISCONNECTED=OFF

Install:

.. code-block:: bash

   sudo make install

v80-smi
=======

Requires C++20 and a built VRT library.

.. code-block:: bash

   cd smi
   mkdir build && cd build
   cmake ..
   make

Install:

.. code-block:: bash

   sudo make install

Examples
========

Each example is a standalone CMake project. To build against the local
repository tree (without installing SLASH first):

.. code-block:: bash

   cd examples/00_axilite
   mkdir build && cd build
   cmake .. -DSLASH_USE_REPO=ON
   make

To build against installed SLASH packages:

.. code-block:: bash

   cmake ..
   make

Building FPGA artefacts (HLS kernels and vrtbin files) requires AMD Vivado and
Vitis HLS. The CMake ``SlashTools`` module provides:

- ``build_hls_dir()`` — compile HLS kernels from a directory.
- ``add_vbin()`` — link kernels into a vrtbin for a target platform
  (``hw``, ``emu``, or ``sim``).
