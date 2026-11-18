# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################
"""Slave-bridge windows through the slash driver (SLASH_QDMA_IOCTL_SLV_*).

The driver allocates host memory in 2 MiB chunks that are contiguous and 2 MiB
aligned in DMA address space (IOMMU or not) and owns BDF entries 0..62: a window
can only point at a chunk of a live buffer, and closing the control fd clears
every window mapped through it. Normally vrtd holds the control fd; this module
is the test-tool side (root, /dev/slash_qdma_ctl*).
"""
import fcntl
import glob
import mmap
import os
import struct

WINDOWS = 63
WINDOW_SIZE = 2 << 20


def _iowr(nr, size):
    return (3 << 30) | (size << 16) | (ord("v") << 8) | nr


BUF_CREATE_FMT = "<IIQII"           # size, flags, length, granule, transfer_hint
INFO_FMT = "<IIIIQQQII"             # size, present, n_windows, iommu, window_size, axi_base, used_mask, brg_err, pad
MAP_FMT = "<IIiIQQ"                 # size, window, buf_fd, pad, buf_offset, dma_addr
UNMAP_FMT = "<II"                   # size, window
SLV_INFO = _iowr(0x5B, struct.calcsize(INFO_FMT))
SLV_BUF_CREATE = _iowr(0x5C, struct.calcsize(BUF_CREATE_FMT))
SLV_MAP = _iowr(0x5D, struct.calcsize(MAP_FMT))
SLV_UNMAP = _iowr(0x5E, struct.calcsize(UNMAP_FMT))


class SlvBuf:
    """A driver buffer: fd + one shared mapping; chunk(i) is a 2 MiB memoryview"""

    def __init__(self, drv, length):
        b = bytearray(struct.pack(BUF_CREATE_FMT, struct.calcsize(BUF_CREATE_FMT), os.O_CLOEXEC, length, 0, 0))
        self.fd = fcntl.ioctl(drv.fd, SLV_BUF_CREATE, b, True)
        self.length = length
        self.mm = mmap.mmap(self.fd, length, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        self.mv = memoryview(self.mm)
        self.views = []                 # every chunk handed out, released by close()

    def chunk(self, i):
        v = self.mv[i * WINDOW_SIZE:(i + 1) * WINDOW_SIZE]
        self.views.append(v)
        return v

    def close(self):
        """Release the views, unmap, close the fd.  The caller has unmapped the
        windows already; a view still exported elsewhere (a cast of a chunk kept
        alive) only delays the munmap to process exit."""
        if self.fd is None:
            return
        for v in self.views + [self.mv]:
            try:
                v.release()
            except BufferError:
                pass
        self.views = []
        try:
            self.mm.close()
        except BufferError:
            pass
        os.close(self.fd)
        self.fd = None


class SlvDriver:
    """The QDMA control device of PF1 of `dev` (e.g. 0000:1b:00)"""

    def __init__(self, dev):
        if dev.count(":") == 1:
            dev = "0000:" + dev
        pf1 = os.path.realpath(f"/sys/bus/pci/devices/{dev.split('.')[0]}.1")
        self.fd = None
        self.node = None
        for node in sorted(glob.glob("/dev/slash_qdma_ctl*")):
            st = os.stat(node)
            link = f"/sys/dev/char/{os.major(st.st_rdev)}:{os.minor(st.st_rdev)}/device"
            if os.path.realpath(link) == pf1:
                self.node = node
                self.fd = os.open(node, os.O_RDWR | os.O_CLOEXEC)
                break
        if self.fd is None:
            raise OSError(f"no /dev/slash_qdma_ctl* for {pf1} (slash driver not loaded?)")

    def info(self):
        b = bytearray(struct.pack(INFO_FMT, struct.calcsize(INFO_FMT), 0, 0, 0, 0, 0, 0, 0, 0))
        fcntl.ioctl(self.fd, SLV_INFO, b, True)
        _, present, n, iommu, wsize, axi, used, brg, _ = struct.unpack(INFO_FMT, b)
        return dict(present=bool(present), n_windows=n, iommu=bool(iommu), window_size=wsize, axi_base=axi,
                    used=[w for w in range(64) if used >> w & 1], brg_err=brg)

    def buf(self, length):
        return SlvBuf(self, length)

    def map(self, window, buf, offset):
        """point `window` at buf[offset:offset + 2 MiB]; returns the host DMA address"""
        b = bytearray(struct.pack(MAP_FMT, struct.calcsize(MAP_FMT), window, buf.fd, 0, offset, 0))
        fcntl.ioctl(self.fd, SLV_MAP, b, True)
        return struct.unpack(MAP_FMT, b)[5]

    def unmap(self, window):
        fcntl.ioctl(self.fd, SLV_UNMAP, bytearray(struct.pack(UNMAP_FMT, struct.calcsize(UNMAP_FMT), window)), True)

    def close(self):
        """closing the control fd also clears every window still mapped through it"""
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
