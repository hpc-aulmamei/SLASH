V80 Runtime Daemon – Client Flow
================================

Overview
--------

``vrtd`` (the *V80 Runtime Daemon*) multiplexes access to SLASH-managed FPGA
devices and enforces permission rules for multi-tenancy. Applications talk to
``vrtd`` over a Unix domain socket via:

- **C API**: *libvrtd* (``<vrtd/vrtd.h>``)
- **C++ wrapper**: *libvrtd++* (``vrtd::Session``, ``vrtd::Device``, ``vrtd::Bar``,
  ``vrtd::BarFile``)

Pipeline
~~~~~~~~

.. code-block::

   +-----------+     +----------+     +-----------+     +---------+     +--------+
   |  libvrt   | <-- | libvrtd++| <-- |  libvrtd  | <-- |  vrtd   | <-- |libslash|
   +-----------+     +----------+     +-----------+     +---------+     +--------+
                                              AF_UNIX / SOCK_SEQPACKET
                                              sendmsg/recvmsg (+SCM_RIGHTS)

Roles
~~~~~

- **SLASH kernel module / libslash**: low-level device control.
- **vrtd**: daemon that arbitrates access and permissions (multi-tenant).
- **libvrtd (C)**: wire protocol client; exposes typed requests/responses.
- **libvrtd++ (C++)**: safer RAII/exception wrapper on top of libvrtd.

Quick Start (C++)
-----------------

Minimal program that opens a session, grabs device 0, opens BAR 0, and reads an
``uint32_t`` via RAII:

.. code-block:: cpp

   #include <vrtd/session.hpp>
   #include <vrtd/device.hpp>
   #include <vrtd/bar.hpp>
   #include <vrtd/bar_file.hpp>
   #include <cstdint>
   #include <iostream>

   int main() {
     try {
       // Prefer the corrected macro name VRTD_STANDARD_PATH if available.
       vrtd::Session s;  // defaults to the standard socket path

       auto n = s.getNumDevices();
       if (n == 0) {
         std::cout << "No devices\n";
         return 0;
       }

       vrtd::Device d = s.getDevice(0);
       vrtd::Bar    b = d.getBar(0);

       vrtd::BarFile bf = b.openBarFile();

       // Start a READ session at offset 0 and access as uint32_t
       auto p = bf.getPtr<std::uint32_t>(vrtd::BarFile::Direction::Read, /*address=*/0);
       std::uint32_t value = *p;  // read via volatile
       (void)value;

       bf.close();   // explicit close (will also run in destructor if no op active)
     } catch (const vrtd::Error& e) {
       std::cerr << "vrtd error: " << e.what() << "\n";
       return 1;
     } catch (const std::exception& e) {
       std::cerr << "std error: " << e.what() << "\n";
       return 1;
     }
     return 0;
   }

Quick Start (C)
---------------

Same operation using the C API and explicit bracketing for memory access:

.. code-block:: c

   #include <vrtd/vrtd.h>
   #include <slash/ctldev.h>
   #include <stdint.h>
   #include <stdio.h>
   #include <unistd.h>

   int main() {
     int fd = vrtd_connect(VRTD_STANDARD_PATH);  // use corrected macro
     if (fd < 0) { perror("vrtd_connect"); return 1; }

     uint32_t num = 0;
     if (vrtd_get_num_devices(fd, &num) != VRTD_RET_OK || num == 0) {
       fprintf(stderr, "no devices or error\n"); close(fd); return 1;
     }

     // Open BAR 0 of device 0 and map it
     struct slash_bar_file bf = {0};
     enum vrtd_ret r = vrtd_open_bar_file(fd, /*dev=*/0, /*bar=*/0, &bf);
     if (r != VRTD_RET_OK) {
       fprintf(stderr, "open bar failed: %d\n", (int)r); close(fd); return 1;
     }

     // READ session for a 32-bit value at offset 0
     slash_bar_file_start_read(&bf);
     volatile uint32_t *p = (volatile uint32_t*)((volatile uint8_t*)bf.map + 0);
     uint32_t value = *p;
     slash_bar_file_end_read(&bf);
     (void)value;

     vrtd_close_bar_file(&bf);  // unmap + close
     close(fd);
     return 0;
   }

End-to-End Flow (C and C++)
---------------------------

This section walks the common path from connection to BAR memory access, showing the **C** and **C++** entry points side-by-side, plus ownership, error, and threading notes at each step. (No full examples—only the exact calls you’ll use.)

1) Connect
~~~~~~~~~~

- **C**
  - Call: ``int fd = vrtd_connect(VRTD_STANDARD_PATH);``
  - On success: ``fd >= 0`` (caller **owns** and must ``close(fd)``).
  - On failure: returns ``-1`` and sets ``errno``.
- **C++**
  - Call: ``vrtd::Session s;`` or ``vrtd::Session s{"/run/vrtd.sock"};``
  - On failure: **throws** ``vrtd::Error(VRTD_RET_BAD_CONN)``.
  - Ownership: RAII; destructor calls ``close()``. ``s.close()`` is explicit.
  - State/introspection: ``s.isClosed()``, ``static_cast<bool>(s)``.
  - Thread-safety: **Thread-safe** (internal mutex).

2) Discover Devices
~~~~~~~~~~~~~~~~~~~

- **C**
  - Count: ``vrtd_get_num_devices(fd, &count)`` → ``VRTD_RET_OK`` on success.
  - Name: ``vrtd_get_device_info(fd, dev_index, name_buf)`` where
    ``char name_buf[128];`` (NUL-terminated; buffer **must be** 128 bytes).
- **C++**
  - Count: ``uint32_t n = s.getNumDevices();``  → may **throw** ``vrtd::Error``.
  - Select: ``vrtd::Device d = s.getDevice(i);`` (0-based; throws
    ``vrtd::Error(VRTD_RET_NOEXIST)`` if out of range).
  - Accessors: ``d.getNum()``, ``d.getName()``.
  - Lifetime note: Any ``Device`` becomes **invalid** if its originating
    ``Session`` is closed or moved; later calls **throw**.

3) BAR Metadata
~~~~~~~~~~~~~~~

- **C**
  - Call: ``vrtd_get_bar_info(fd, dev, bar, &info)`` where
    ``struct slash_ioctl_bar_info info;``
  - Returns: Usability, in-use flag, physical start address, length (bytes).
- **C++**
  - Call: ``vrtd::Bar b = d.getBar(bar_index);`` → may **throw** ``vrtd::Error``.
  - Query: ``b.isUsable()``, ``b.isInUse()`` (currently always ``false``),
    ``b.getStartAddress()``, ``b.getLength()`` (both **bytes**, physical).
  - Lifetime note: ``Bar`` is **invalidated** if its session is closed/moved.

4) Obtain BAR FD (for mmap)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

- **C**
  - Call: ``vrtd_get_bar_fd(fd, dev, bar, &bar_fd, &len)``
  - Behavior: Receives ``bar_fd`` via ``SCM_RIGHTS``; caller **owns** and must ``close(bar_fd)`` when done (or use Step 5 helper).
- **C++**
  - Not typically called directly; the wrapper performs this internally in
    ``Bar::openBarFile()``.

5) Map the BAR
~~~~~~~~~~~~~~

- **C**
  - Call: ``vrtd_open_bar_file(fd, dev, bar, &bf)`` where
    ``struct slash_bar_file bf;``
  - On success: ``bf.fd`` valid, ``bf.map`` is mapped, ``bf.len`` is size.
  - Unmap/close: ``vrtd_close_bar_file(&bf);`` (safe to call multiple times; no-op if already unmapped).
- **C++**
  - Call: ``vrtd::BarFile bf = b.openBarFile();``  → may **throw** ``vrtd::Error``.
  - Ownership: RAII; ``BarFile`` owns FD + mapping.
  - Close: ``bf.close();`` / destructor. **May throw** if an operation is still in progress (see Step 6).
  - State: ``bf.isClosed()``, ``bf.getLen()``.

6) Access BAR Memory
~~~~~~~~~~~~~~~~~~~~

- **C**
  - **Read**: ``slash_bar_file_start_read(&bf);`` … access via
    ``volatile`` pointer into ``bf.map`` … ``slash_bar_file_end_read(&bf);``
  - **Write**: ``slash_bar_file_start_write(&bf);`` … access … ``slash_bar_file_end_write(&bf);``
  - Notes:
    - Use ``(volatile uint8_t*)bf.map + offset`` to compute addresses.
    - Ensure alignment and bounds (``offset + sizeof(T) <= bf.len``).
    - Access semantics use ``volatile``.
- **C++**
  - Preferred: ``auto p = bf.getPtr<T>(vrtd::BarFile::Direction::Read /*or Write*/, offset);``
    - Returns a **move-only** ``vrtd::BarFilePtr<T>`` that brackets the operation and
      ends it automatically on destruction.
    - Misuse throws ``std::runtime_error`` (closed, bad address, or another op in progress).
    - Only one operation (read **or** write) may be active at a time per ``BarFile``.
    - ``T`` should be trivially copyable/standard-layout; access is via **``volatile``**.
  - Raw pointer (advanced): ``bf.getRawPtr(offset)`` → ``volatile void*``
    - **Caller must** manually call the appropriate ``slash_bar_file_start_*`` /
      ``_end_*`` functions; recommended only when RAII bracketing cannot be used.

Error Model & Mapping
---------------------

- **C** functions return ``vrtd_ret`` (check for ``VRTD_RET_OK`` before using outputs).
- **C++** methods **throw** ``vrtd::Error``; transport/socket failures map to
  ``VRTD_RET_BAD_CONN``. ``vrtd::Error::what()`` returns a static, human-readable string.
- Local misuse in ``BarFile``/``BarFilePtr`` (e.g., overlapping ops, bad address) throws ``std::runtime_error``.

Thread Safety Summary
~~~~~~~~~~~~~~~~~~~~~

- **Session / Device / Bar (C++)**: Public methods are **thread-safe** (internal mutex).
  Objects remain **logically** tied to the lifetime of their originating ``Session``.
- **BarFile / BarFilePtr (C++)**: **Not thread-safe**. At most one active read/write
  operation per ``BarFile``. ``close()`` / destructor may **throw** if an operation is active.

Lifetime & Moves (C++)
~~~~~~~~~~~~~~~~~~~~~~

- Moving or closing a ``Session`` invalidates previously obtained ``Device`` and ``Bar`` objects (their methods will throw thereafter).
- ``BarFile`` is move-only. Ensure all ``BarFilePtr``s have been destroyed **before**
  calling ``bf.close()`` or letting the destructor run, otherwise an exception may be thrown.


Wire Protocol (High-Level)
--------------------------

- Transport: **AF_UNIX** + ``SOCK_SEQPACKET``.
- Messages: request/response headers (size, opcode, seqno) + body.
- FD passing: responses may carry a file descriptor using ``SCM_RIGHTS``
  (e.g., for BAR file access).
- Size limits: request body must respect protocol bounds
  (e.g. ``VRTD_MSG_MAX_SIZE`` minus headers).
- Generic escape hatch: ``vrtd_raw_request`` sends arbitrary opcodes; most
  users should prefer typed helpers.

C API at a Glance
-----------------

- ``vrtd_connect(path)`` → returns ``fd`` (close with ``close()``).
- ``vrtd_get_num_devices(fd, &out)`` → device count.
- ``vrtd_get_device_info(fd, dev, name[128])`` → device name (NUL-terminated).
- ``vrtd_get_bar_info(fd, dev, bar, &info)`` → BAR metadata.
- ``vrtd_get_bar_fd(fd, dev, bar, &fd_out, &len)`` → BAR FD + length.
- ``vrtd_open_bar_file(fd, dev, bar, &slash_bar_file)`` → maps BAR.
- ``vrtd_close_bar_file(&slash_bar_file)`` → unmaps/closes BAR.
- ``vrtd_raw_request(fd, opcode, ...)`` → low-level request/response.

C++ Wrapper Flow
----------------

- ``vrtd::Session``: owns the connection; **thread-safe** (internal mutex).
  - ``getNumDevices()``, ``getDevice(i)``, ``close()``, ``operator bool``.
- ``vrtd::Device``: value-type view of a device (number + name).
  - **Invalidated** if its originating session is closed or moved.
  - ``getBar(bar_index)`` → ``vrtd::Bar``.
- ``vrtd::Bar``: value-type BAR metadata and opener.
  - **Invalidated** if the originating session is closed or moved.
  - ``openBarFile()`` → ``vrtd::BarFile`` (owns FD + mapping).
- ``vrtd::BarFile``: **not thread-safe**; single in-flight op.
  - ``getPtr<T>(Direction, offset)`` → move-only pointer that brackets
    read/write operations and ends them on destruction.
  - ``getRawPtr(offset)`` → volatile pointer (caller must bracket manually).
  - ``close()`` / destructor may **throw** if an op is still active.

Error Model
-----------

- All typed C functions return ``vrtd_ret``. Success is ``VRTD_RET_OK``.
- The C++ API throws ``vrtd::Error``; ``what()`` returns a static
  human-readable string mapped from the code (no allocation).

Common codes:

- ``VRTD_RET_OK`` — success.
- ``VRTD_RET_BAD_LIB_CALL`` — bad library usage (e.g., null out-pointer).
- ``VRTD_RET_BAD_CONN`` — broken/absent transport (socket errors, etc.).
- ``VRTD_RET_BAD_REQUEST`` — malformed request.
- ``VRTD_RET_INVALID_ARGUMENT`` — invalid argument.
- ``VRTD_RET_NOEXIST`` — resource does not exist (e.g., out-of-range index).
- ``VRTD_RET_INTERNAL_ERROR`` — daemon-side failure; check vrtd logs.
- ``VRTD_RET_AUTH_ERROR`` — permission error.

Thread Safety
-------------

- **Session / Device / Bar (C++)**: public methods are **thread-safe**; they
  synchronize on the owning session’s mutex. However, *object validity* is tied
  to the session—closing or moving a session invalidates previously obtained
  ``Device``/``Bar`` values.
- **BarFile / BarFilePtr (C++)**: **not thread-safe**. Only one read or write
  operation may be active at a time on a single ``BarFile``. Re-entrant calls
  (e.g., two ``getPtr()``s) throw.

Lifetime & Moves (C++)
----------------------

- Moving a ``Session`` closes the moved-from object. Any ``Device``/``Bar`` from
  that session become invalid; subsequent calls on them will throw.
- ``BarFile`` is move-only. Its destructor (or ``close()``) releases resources.
  If a read/write operation is active (a ``BarFilePtr`` still alive),
  destruction/close may throw to signal misuse.

Addressing & Access Semantics
-----------------------------

- BAR addresses returned by metadata are **physical**. ``getStartAddress()`` and
  ``getLength()`` are in **bytes**.
- ``BarFile::getPtr<T>()`` checks only ``address < len``—the caller must ensure
  alignment and that ``address + sizeof(T) <= len``.
- Accesses are through **``volatile``** pointers to model device memory.

Notes & Compatibility
---------------------

- The intended default socket macro name is ``VRTD_STANDARD_PATH``. If you see a
  misspelled macro in older headers, prefer passing an explicit path or define
  ``VRTD_STANDARD_PATH`` accordingly.
- Use typed helpers where possible; ``vrtd_raw_request`` is for advanced usage.

Troubleshooting
---------------

- **Out-of-range device index** → ``VRTD_RET_NOEXIST`` (C) or ``vrtd::Error`` (C++).
- **Session closed/moved, then using Device/Bar** → throws (invalid lifetime).
- **Two concurrent ``getPtr()`` calls on the same BarFile** → throws re-entrancy error.
- **Transport errors** (socket down, daemon not running) → map to
  ``VRTD_RET_BAD_CONN`` / ``vrtd::Error`` with "connection" message.

