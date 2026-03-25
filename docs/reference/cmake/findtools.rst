..
   comment:: SPDX-License-Identifier: MIT
   comment:: Copyright (C) 2025 Advanced Micro Devices, Inc

##############################
FindVivado and FindVitis
##############################

SLASH provides two CMake find modules for locating AMD Vivado and Vitis HLS
installations. These are used internally by ``SlashTools.cmake`` and
``BuildHLS.cmake`` but can also be included directly.

FindVivado
==========

Locates an AMD Vivado installation for FPGA synthesis and implementation.

Search Order
------------

1. ``VIVADO_ROOT_DIR`` CMake variable.
2. ``XILINX_VIVADO`` environment variable.
3. System ``PATH`` (searches for ``vivado`` in ``bin/`` subdirectories).

Variables Set
-------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Variable
     - Description
   * - ``VIVADO_FOUND``
     - ``TRUE`` if Vivado was located.
   * - ``VIVADO_ROOT_DIR``
     - Root directory of the Vivado installation.
   * - ``VIVADO_BINARY``
     - Full path to the ``vivado`` executable.

Usage
-----

``FindVivado`` is included automatically by ``SlashTools.cmake`` — no
manual ``include()`` is needed in most projects.

To point CMake at a non-standard Vivado installation:

.. code-block:: bash

   # Via CMake variable
   cmake .. -DVIVADO_ROOT_DIR=/tools/Xilinx/Vivado/2024.2

   # Or via environment variable
   export XILINX_VIVADO=/tools/Xilinx/Vivado/2024.2
   cmake ..

Error Behaviour
---------------

``FindVivado`` issues a ``FATAL_ERROR`` if the ``vivado`` binary cannot be
found.

FindVitis
=========

Locates an AMD Vitis HLS installation for kernel compilation.

Search Order
------------

1. ``VITIS_ROOT_DIR`` CMake variable.
2. ``XILINX_VITIS`` environment variable.
3. ``VITIS_HOME`` environment variable.
4. ``VITIS`` environment variable.
5. System ``PATH`` (searches for ``vitis`` in ``bin/`` subdirectories).

Variables Set
-------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Variable
     - Description
   * - ``VITIS_FOUND``
     - ``TRUE`` if Vitis was located.
   * - ``VITIS_BINARY``
     - Full path to the ``vitis`` executable.
   * - ``VITIS_ROOT_DIR``
     - Root directory of the Vitis installation.
   * - ``VITIS_INCLUDE_DIR``
     - Path to the Vitis include directory (e.g. for ``ap_fixed.h``,
       ``hls_stream.h``). Validated to exist.

Usage
-----

``FindVitis`` is included automatically by ``BuildHLS.cmake``. To override
the detected installation:

.. code-block:: bash

   # Via CMake variable
   cmake .. -DVITIS_ROOT_DIR=/tools/Xilinx/Vitis_HLS/2024.2

   # Or via environment variable
   export XILINX_VITIS=/tools/Xilinx/Vitis_HLS/2024.2
   cmake ..

Error Behaviour
---------------

``FindVitis`` issues a ``FATAL_ERROR`` if:

- The ``vitis`` binary cannot be found.
- The include directory (``${VITIS_ROOT_DIR}/include``) does not exist.
