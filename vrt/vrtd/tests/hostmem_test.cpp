/**
 * The MIT License (MIT)
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * hostmem against a fake service BAR (plain memory holding sl_vmem's
 * registers and table) and fake driver windows: the libslash SLV calls are
 * replaced with -Wl,--wrap (see CMakeLists.txt).
 */

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <map>
#include <set>
#include <vector>

extern "C" {
#include "hostmem.h"
}

namespace {

constexpr uint32_t SLV = HOSTMEM_SLV_OFFSET;
constexpr uint64_t VA_BASE = 0x20800000000ull;
constexpr uint64_t PAGE = 2ull << 20;

struct FakeDriver {
    bool present = true;
    uint64_t used_preset = 0;
    std::map<uint32_t, std::pair<int, uint64_t>> windows;   // window -> (buf fd, offset)
    std::vector<uint32_t> unmapped;
    int fail_map_at = -1;     // fail the n-th map call
    int map_calls = 0;
    std::set<int> fds;
};

FakeDriver *drv = nullptr;

}  // namespace

extern "C" {

int __wrap_slash_qdma_slv_info(struct slash_qdma *, struct slash_qdma_slv_info *info)
{
    memset(info, 0, sizeof(*info));
    info->present = drv->present ? 1 : 0;
    info->n_windows = SLASH_QDMA_SLV_WINDOWS;
    info->iommu = 1;
    info->window_size = SLASH_QDMA_SLV_WINDOW_SIZE;
    info->axi_base = SLASH_QDMA_SLV_AXI_BASE;
    info->used_mask = drv->used_preset;
    return 0;
}

int __wrap_slash_qdma_slv_buffer_create(struct slash_qdma *, uint64_t length)
{
    if (length == 0 || length % PAGE) {
        errno = EINVAL;
        return -1;
    }
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    drv->fds.insert(fd);
    return fd;
}

int __wrap_slash_qdma_slv_map(struct slash_qdma *, uint32_t window, int buf_fd, uint64_t offset, uint64_t *dma)
{
    if (drv->map_calls++ == drv->fail_map_at || drv->windows.count(window)) {
        errno = EBUSY;
        return -1;
    }
    drv->windows[window] = {buf_fd, offset};
    if (dma != nullptr) {
        *dma = 0x100000000ull + window * PAGE;
    }
    return 0;
}

int __wrap_slash_qdma_slv_unmap(struct slash_qdma *, uint32_t window)
{
    drv->unmapped.push_back(window);
    if (drv->windows.erase(window) == 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

}  // extern "C"

class HostmemTest : public ::testing::Test {
   protected:
    std::vector<uint32_t> mem;            // the service BAR
    struct slash_bar_file bar {};
    struct slash_bar_file *slot = &bar;
    struct slash_qdma qdma {};
    FakeDriver fake;
    struct hostmem *hm = nullptr;

    uint32_t &reg(uint32_t off) { return mem[(SLV + off) / 4]; }
    uint32_t &entry(uint32_t vpn) { return mem[(SLV + 0x10000 + 4 * vpn) / 4]; }

    void SetUp() override {
        drv = &fake;
        mem.assign((SLV + 0x40000) / 4, 0);
        bar.map = mem.data();
        bar.len = mem.size() * 4;
        bar.fd = -1;
        reg(0x00) = 0x534C564D;                 // ID
        reg(0x04) = 0x00010000;                 // VERSION
        reg(0x0C) = 0x40154000;                 // CAPS: 64 windows, 2 MiB, 16384 pages
        reg(0x14) = 0x2;                        // STATUS: drained
        reg(0x18) = 63;
        reg(0x40) = 0xE8000000;                 // WIN_BASE
        reg(0x48) = (uint32_t) VA_BASE;
        reg(0x4C) = (uint32_t) (VA_BASE >> 32);
        entry(5) = 0x0107;                      // stale entry from an earlier session
        qdma.fd = -1;
    }

    void TearDown() override {
        cleanup_hostmem(hm);
        drv = nullptr;
    }

    void open_hm() {
        hm = hostmem_open(&qdma, &slot);
        ASSERT_NE(hm, nullptr);
    }
};

TEST_F(HostmemTest, AbsentWithoutTranslator) {
    reg(0x00) = 0;
    errno = 0;
    EXPECT_EQ(hostmem_open(&qdma, &slot), nullptr);
    EXPECT_EQ(errno, ENODEV);
}

TEST_F(HostmemTest, AbsentWithoutDriverWindows) {
    fake.present = false;
    EXPECT_EQ(hostmem_open(&qdma, &slot), nullptr);
}

TEST_F(HostmemTest, OpenResetsTheTranslator) {
    open_hm();
    EXPECT_EQ(entry(5), 0u);
    EXPECT_EQ(reg(0x18), 63u);                  // WIN_LIMIT
    EXPECT_EQ(reg(0x10), 1u);                   // CTRL: enabled, not paused
    EXPECT_EQ(reg(0x24), 3u);                   // CLEAR: faults and counters
    EXPECT_TRUE(hostmem_present(hm));
}

TEST_F(HostmemTest, BufferMapsPagesToWindowsAndTable) {
    open_hm();
    int fd = -1;
    uint64_t addr = 0, size = 0;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, 3 << 20, &fd, &addr, &size), VRTD_RET_OK);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(addr, VA_BASE);
    EXPECT_EQ(size, 2 * PAGE);
    ASSERT_EQ(fake.windows.size(), 2u);
    std::set<uint32_t> wins;
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t e = entry(i);
        EXPECT_EQ(e & 0xFF, 0x7u) << "valid, read, write";
        uint32_t w = e >> 8;
        ASSERT_TRUE(fake.windows.count(w));
        EXPECT_EQ(fake.windows[w].first, fd);
        EXPECT_EQ(fake.windows[w].second, i * PAGE);
        wins.insert(w);
    }
    EXPECT_EQ(wins.size(), 2u);
    EXPECT_EQ(entry(2), 0u);
    EXPECT_EQ(reg(0x10), 1u) << "resumed after mapping";
}

TEST_F(HostmemTest, SecondBufferGetsNextPagesAndOtherWindows) {
    open_hm();
    int fd1, fd2;
    uint64_t a1, a2, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd1, &a1, &s), VRTD_RET_OK);
    ASSERT_EQ(hostmem_buffer_open(hm, 8, 0, 2 * PAGE, &fd2, &a2, &s), VRTD_RET_OK);
    EXPECT_EQ(a2, VA_BASE + PAGE);
    EXPECT_EQ(fake.windows.size(), 3u);
    EXPECT_NE(entry(0) >> 8, entry(1) >> 8);
}

TEST_F(HostmemTest, ReadOnlyEntry) {
    open_hm();
    int fd;
    uint64_t a, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, VRTD_HOST_BUFFER_READ, PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(entry(0) & 0xFF, 0x3u);
}

TEST_F(HostmemTest, CloseInvalidatesThenUnmapsAndFreesWindows) {
    open_hm();
    int fd;
    uint64_t a, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, 2 * PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(hostmem_buffer_close(hm, 8, a), VRTD_RET_AUTH_ERROR) << "other client";
    EXPECT_EQ(hostmem_buffer_close(hm, 7, a + PAGE), VRTD_RET_NOEXIST);
    ASSERT_EQ(hostmem_buffer_close(hm, 7, a), VRTD_RET_OK);
    EXPECT_EQ(entry(0), 0u);
    EXPECT_EQ(entry(1), 0u);
    EXPECT_TRUE(fake.windows.empty());
    EXPECT_EQ(fake.unmapped.size(), 2u);
    // the pages and windows are free again
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, 63 * PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(a, VA_BASE);
}

TEST_F(HostmemTest, ClientCleanupClosesOnlyItsBuffers) {
    open_hm();
    int fd;
    uint64_t a7, a8, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a7, &s), VRTD_RET_OK);
    ASSERT_EQ(hostmem_buffer_open(hm, 8, 0, PAGE, &fd, &a8, &s), VRTD_RET_OK);
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a7, &s), VRTD_RET_OK);
    hostmem_cleanup_client(hm, 7);
    EXPECT_EQ(fake.windows.size(), 1u);
    EXPECT_EQ(entry(0), 0u);
    EXPECT_NE(entry(1), 0u);
    EXPECT_EQ(entry(2), 0u);
    EXPECT_EQ(hostmem_buffer_close(hm, 8, a8), VRTD_RET_OK);
}

TEST_F(HostmemTest, LimitsAndExhaustion) {
    open_hm();
    int fd;
    uint64_t a, s;
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, 64 * PAGE, &fd, &a, &s), VRTD_RET_INVALID_ARGUMENT);
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, 0, &fd, &a, &s), VRTD_RET_INVALID_ARGUMENT);
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 8, PAGE, &fd, &a, &s), VRTD_RET_INVALID_ARGUMENT);
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, 60 * PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, 4 * PAGE, &fd, &a, &s), VRTD_RET_BUSY);
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, 3 * PAGE, &fd, &a, &s), VRTD_RET_OK);
}

TEST_F(HostmemTest, WindowsInUseElsewhereAreSkipped) {
    fake.used_preset = 0x3;     // windows 0 and 1 mapped by another fd
    open_hm();
    int fd;
    uint64_t a, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(entry(0) >> 8, 2u);
}

TEST_F(HostmemTest, MapFailureRollsBack) {
    open_hm();
    fake.fail_map_at = 1;       // second window fails
    int fd;
    uint64_t a, s;
    EXPECT_NE(hostmem_buffer_open(hm, 7, 0, 2 * PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(entry(0), 0u);
    EXPECT_EQ(entry(1), 0u);
    EXPECT_TRUE(fake.windows.empty()) << "the first window is unmapped again";
    EXPECT_EQ(reg(0x10), 1u) << "resumed";
    fake.fail_map_at = -1;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a, &s), VRTD_RET_OK);
    EXPECT_EQ(a, VA_BASE) << "the pages are free again";
}

TEST_F(HostmemTest, NotDrainedRefusesToMap) {
    open_hm();
    reg(0x1C) = 1;              // OUTSTANDING: one write never completes
    int fd;
    uint64_t a, s;
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a, &s), VRTD_RET_BUSY);
    EXPECT_TRUE(fake.windows.empty());
    EXPECT_EQ(entry(0), 0u);
}

TEST_F(HostmemTest, TranslatorGoneMeansNotPresent) {
    open_hm();
    reg(0x00) = 0;
    EXPECT_FALSE(hostmem_present(hm));
    int fd;
    uint64_t a, s;
    EXPECT_EQ(hostmem_buffer_open(hm, 7, 0, PAGE, &fd, &a, &s), VRTD_RET_NOEXIST);
}

TEST_F(HostmemTest, CleanupDisablesAndUnmapsEverything) {
    open_hm();
    int fd;
    uint64_t a, s;
    ASSERT_EQ(hostmem_buffer_open(hm, 7, 0, 2 * PAGE, &fd, &a, &s), VRTD_RET_OK);
    cleanup_hostmem(hm);
    hm = nullptr;
    EXPECT_TRUE(fake.windows.empty());
    EXPECT_EQ(entry(0), 0u);
    EXPECT_EQ(reg(0x10), 0u);
}
