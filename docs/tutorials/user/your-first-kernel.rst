..
   comment:: SPDX-License-Identifier: MIT
   comment:: Copyright (C) 2025 Advanced Micro Devices, Inc

##################
Your First Kernel
##################

This tutorial walks through writing an HLS kernel from scratch, compiling it
with Vitis HLS, linking it into a vrtbin, and running it on a V80 board. By the
end you will understand every file in a minimal SLASH project.

Prerequisites
=============

- The SLASH stack is installed (kernel module, libslash, vrtd, VRT, v80-smi).
  See :doc:`/howto/build-from-source` if building from source.
- AMD Vivado **2024.2** and Vitis HLS are installed and on ``PATH``.
- A V80 board is installed and visible (``v80-smi list``), or you plan to use
  emulation.

Anatomy of an HLS Kernel
=========================

An HLS kernel is a C/C++ function with interface pragmas that tell Vitis HLS
how to map arguments to hardware ports. Here is the ``increment`` kernel from
``examples/00_axilite/hls/increment.cpp``:

.. code-block:: cpp

   #include <ap_fixed.h>
   #include <hls_stream.h>

   void increment(ap_uint<32> size, float* in, hls::stream<float>& axis_out) {
   #pragma hls interface mode=s_axilite port=size
   #pragma hls interface m_axi bundle=gmem0 port=in max_widen_bitwidth=64
   #pragma hls interface axis port=axis_out
   #pragma hls interface mode=s_axilite port=return

       for(ap_uint<32> i = 0; i < size; i++) {
       #pragma hls pipeline II=1
           float data = in[i] + 1;
           axis_out.write(data);
       }
   }

Each pragma controls a different aspect of the hardware interface:

``s_axilite``
   Exposes the argument as a memory-mapped register on the AXI-Lite control
   bus. The host sets these via ``Kernel::setArg()`` before launching the
   kernel.

``m_axi``
   Maps a pointer argument to an AXI memory-mapped master port. The
   ``bundle=gmem0`` name becomes the port name visible in the linker
   configuration (prefixed with ``m_axi_``). ``max_widen_bitwidth=64``
   limits data-path widening for this port.

``axis``
   Maps an ``hls::stream`` argument to an AXI-Stream port. Streams connect
   kernels directly without going through device memory.

``s_axilite port=return``
   Required on every SLASH kernel. It creates the AP_START / AP_DONE / AP_IDLE
   control registers that VRT uses to start and poll the kernel.

``pipeline II=1``
   Instructs HLS to pipeline the loop body with an initiation interval of one
   clock cycle — one new iteration begins every cycle.

HLS Configuration File
=======================

Each kernel needs a ``.cfg`` file that tells Vitis HLS how to compile it.
Here is ``increment.cfg``:

.. code-block:: ini

   part=xcv80-lsva4737-2MHP-e-S

   [hls]
   flow_target=vivado

   syn.top=increment
   syn.file=increment.cpp
   clock=4ns

   package.output.format=ip_catalog
   package.output.syn=false

``part``
   The FPGA part string for the V80 board.

``syn.top``
   The top-level function name in the C++ source.

``syn.file``
   The source file to compile.

``clock``
   The target clock period. ``4ns`` corresponds to 250 MHz.

``package.output.format``
   Must be ``ip_catalog`` so the output can be consumed by the SLASH linker.

``package.output.syn``
   Set to ``false`` to skip RTL synthesis during HLS (the linker handles it).

Linker Configuration
====================

The linker configuration file (``config.cfg``) describes how kernels are
instantiated, connected, and mapped to memory. Here is
``examples/00_axilite/config.cfg``:

.. code-block:: ini

   [connectivity]
   nk=accumulate:1:accumulate_0
   nk=increment:1:increment_0
   stream_connect=increment_0.axis_out:accumulate_0.axis_in
   sp=increment_0.m_axi_gmem0:HBM1

``nk``
   Instantiates a kernel. Format: ``<kernel_name>:<count>:<instance_name>``.
   The instance name is what you pass to ``vrt::Kernel(device, "increment_0")``.

``stream_connect``
   Wires an AXI-Stream output port on one kernel instance to an input port on
   another. Format: ``<src_instance>.<port>:<dst_instance>.<port>``.

``sp``
   Maps an AXI memory-mapped port to a physical memory resource. Format:
   ``<instance>.<port>:<memory>``. Valid memories include ``HBM0``–``HBM63``
   and ``DDR0``.

CMake Build Setup
=================

SLASH provides CMake modules for compiling HLS kernels and linking vrtbins.
Here is the pattern from ``examples/00_axilite/CMakeLists.txt``:

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.20)
   project(00_axilite LANGUAGES CXX)
   set(CMAKE_CXX_STANDARD 20)

   option(SLASH_USE_REPO "Build against local repo tree" OFF)

   if(SLASH_USE_REPO)
     get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
     list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
     include(SlashTools)
     add_subdirectory(${REPO_ROOT}/vrt ${CMAKE_CURRENT_BINARY_DIR}/vrt)
     set(_VRT_LIBS vrt)
   else()
     find_package(vrt REQUIRED CONFIG)
     find_package(SlashTools REQUIRED)
     set(_VRT_LIBS vrt::vrt)
   endif()

   # --- HLS kernels ---
   set(DEVICE "xcv80-lsva4737-2MHP-e-S" CACHE STRING "Target device")

   build_hls_dir(
     TARGET      hls
     ROOT        "${CMAKE_CURRENT_SOURCE_DIR}/hls"
     DEVICE      "${DEVICE}"
     KERNELS     increment accumulate
     OUT_KERNELS _KERNELS
   )

   # --- VBIN targets ---
   set(CFG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config.cfg")
   add_vbin(TARGET "axilite_hw"  PLATFORM "hw"  CFG "${CFG_FILE}" KERNELS ${_KERNELS})
   add_vbin(TARGET "axilite_emu" PLATFORM "emu" CFG "${CFG_FILE}" KERNELS ${_KERNELS})

   # --- Executable ---
   add_executable(${PROJECT_NAME} 00_axilite.cpp)
   target_link_libraries(${PROJECT_NAME} PRIVATE ${_VRT_LIBS})

``build_hls_dir()`` compiles every kernel in the ``hls/`` directory. It
expects ``<name>.cpp`` and ``<name>.cfg`` file pairs for each kernel listed in
``KERNELS``. The compiled IP paths are stored in ``_KERNELS``.

``add_vbin()`` invokes the SLASH linker (``v80++``) to produce a ``.vbin``
archive from the compiled kernels and the connectivity configuration. One
target is created per platform (``hw``, ``emu``, ``sim``).

When building from the SLASH source tree, pass ``-DSLASH_USE_REPO=ON`` so CMake
finds the modules and VRT library locally. When SLASH is installed system-wide,
``find_package`` handles everything automatically.

See :doc:`/reference/cmake/slashtools` and :doc:`/reference/cmake/buildhls`
for full function reference.

Build and Run
=============

.. code-block:: bash

   cd examples/00_axilite
   mkdir build && cd build
   cmake .. -DSLASH_USE_REPO=ON
   make              # build the host application
   make hls          # compile HLS kernels (requires Vitis HLS)
   make axilite_hw   # link into a hardware vrtbin

Run the application:

.. code-block:: bash

   v80-smi list                            # find your board's BDF
   ./00_axilite 03:00 axilite_hw.vbin      # run with BDF and vrtbin

For emulation (no FPGA required):

.. code-block:: bash

   make axilite_emu
   ./00_axilite 03:00 axilite_emu.vbin

Next Steps
==========

- :doc:`buffers-and-memory` — learn about DDR vs HBM memory and buffer
  management.
- :doc:`/reference/cmake/slashtools` — full ``add_vbin()`` reference.
- :doc:`/reference/cmake/buildhls` — full ``build_hls()`` and
  ``build_hls_dir()`` reference.
- :doc:`/explanation/platform-modes` — run the same code in emulation or
  simulation.
