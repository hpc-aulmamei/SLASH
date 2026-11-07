"""PF2 BAR2 of the SLASH V80 (slash_ctl): single 32-bit loads and stores."""
import mmap
import os

BAR2_SIZE = 128 << 20            # mapped part of PF2 BAR2


class Bar:
    def __init__(self, bdf):
        if bdf.count(".") == 0:
            bdf = bdf + ".2"            # SLASH PF2 (slash_ctl)
        if bdf.count(":") == 1:
            bdf = "0000:" + bdf
        path = f"/sys/bus/pci/devices/{bdf}/resource2"
        self.fd = os.open(path, os.O_RDWR | os.O_SYNC)
        self.mm = mmap.mmap(self.fd, BAR2_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
        # One 32-bit load or store per access. struct.pack_into zero-fills the
        # target first, so each write reached the card as four zero byte writes
        # and then the word itself.
        self.mv = memoryview(self.mm).cast("I")

    def rd(self, off):
        return self.mv[off >> 2]

    def wr(self, off, val):
        self.mv[off >> 2] = val & 0xFFFFFFFF
