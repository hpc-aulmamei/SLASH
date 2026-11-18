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
 * host_buffer_test: a user-region kernel on host memory through vrtd.
 *
 * Everything goes through vrtd (libvrtdpp), as an application would: vrtd
 * opens the host buffer (driver memory + slave-bridge windows + translator
 * table), the test only gets the buffer mapping and BAR0 for the kernel.
 * The kernel is traffic_virt_4 (hbm_bandwidth HLS) of the rpuq static's
 * default user region, whose m_axi is the user region's HOST port: it reads
 * (XOR of bits 31:0 of every 256-bit word) or writes (word i = i) 512 MiB from
 * its pointer; the pages outside the buffer fault in the translator.
 *
 *   VRTD_SOCKET=/run/vrtd-slv.sock ./host_buffer_test 0000:1b:00 [pages] [steps]
 */

#include <vrtd/session.hpp>
#include <vrtd/device.hpp>
#include <vrtd/error.hpp>
#include <vrtd/host_buffer.hpp>

#include <unistd.h>

#include <chrono>
#include <cstdarg>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

namespace {

constexpr uint64_t KCTRL = 0x580000;   // traffic_virt_4 in PF2 BAR0
constexpr uint32_t K_AP = 0x00, K_PTR = 0x10, K_WR = 0x1C, K_ACC = 0x24;
constexpr uint64_t K_BYTES = 512ull << 20;
constexpr uint64_t BURST = 256;        // bytes per request at the translator (AxLEN 15, 128 bit)

int passes = 0, fails = 0;

bool check(const char *name, bool ok, const std::string &detail)
{
    std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    (ok ? passes : fails)++;
    return ok;
}

std::string fmt(const char *f, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    return b;
}

struct Kernel {
    vrtd::BarFile &bar;
    uint32_t rd(uint32_t off) {
        auto p = bar.getPtr<uint32_t>(vrtd::BarFile::Direction::Read, KCTRL + off);
        return *p;
    }
    void wr(uint32_t off, uint32_t v) {
        auto p = bar.getPtr<uint32_t>(vrtd::BarFile::Direction::Write, KCTRL + off);
        *p = v;
    }
    // returns seconds; throws on timeout
    double run(uint64_t ptr, uint32_t write_mode) {
        wr(K_PTR, (uint32_t) ptr);
        wr(K_PTR + 4, (uint32_t) (ptr >> 32));
        wr(K_WR, write_mode);
        auto t0 = std::chrono::steady_clock::now();
        wr(K_AP, 1);
        for (;;) {
            uint32_t a = rd(K_AP);
            if ((a & 4) && !(a & 1)) {
                break;
            }
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(20)) {
                throw std::runtime_error(fmt("kernel still running after 20 s (ap_ctrl 0x%08x)", a));
            }
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
};

uint32_t xor_low32(const uint32_t *w, uint64_t bytes)
{
    uint32_t x = 0;
    for (uint64_t i = 0; i < bytes / 4; i += 8) {
        x ^= w[i];
    }
    return x;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <bdf, e.g. 0000:1b:00> [pages=62] [steps=read,write]\n", argv[0]);
        return 2;
    }
    std::string bdf = argv[1];
    uint32_t pages = argc > 2 ? (uint32_t) std::strtoul(argv[2], nullptr, 0) : 62;
    std::string steps = argc > 3 ? argv[3] : "read,write";

    try {
        vrtd::Session s;
        vrtd::Device d = s.getDeviceByBdf(bdf);
        std::printf("device %u %s (%s)\n", d.getNum(), std::string(d.getName()).c_str(), bdf.c_str());

        // 1) the service BAR stays inside vrtd
        try {
            vrtd::BarFile b2 = d.getBar(2).openBarFile();
            check("service BAR (2) refused to clients", false, "GET_BAR_FD 2 succeeded");
        } catch (const vrtd::Error &e) {
            check("service BAR (2) refused to clients", e.getErrorCode() == VRTD_RET_AUTH_ERROR, e.what());
        }

        // 2) translator state
        vrtd::HostMemInfo info = d.getHostMemInfo();
        if (!check("host-memory translator present", info.present,
                   fmt("windows %u (%u free), %u KiB pages, aperture 0x%" PRIx64 " + %" PRIu64 " GiB, IOMMU %s",
                       info.windowsTotal, info.windowsFree, info.pageSize >> 10, info.vaBase,
                       info.vaSize >> 30, info.iommu ? "translating" : "off/pass-through"))) {
            return 1;
        }

        // 3) BAR0 and the kernel
        vrtd::BarFile bar0 = d.getBar(0).openBarFile();
        Kernel k{bar0};
        uint32_t ap = k.rd(K_AP);
        if (!check("traffic_virt_4 present and idle", (ap & 4) && !(ap & 1), fmt("ap_ctrl 0x%08x", ap))) {
            return 1;
        }

        // 4) the host buffer
        uint64_t want = (uint64_t) pages * info.pageSize;
        vrtd::HostBuffer hb = d.openHostBuffer(want);
        vrtd::HostMemInfo info2 = d.getHostMemInfo();
        check("host buffer opened", hb.size() == want && hb.data() != nullptr &&
                  info2.windowsFree + pages == info.windowsFree,
              fmt("card address 0x%" PRIx64 ", %" PRIu64 " MiB, windows free %u -> %u", hb.cardAddr(),
                  hb.size() >> 20, info.windowsFree, info2.windowsFree));
        auto *w = static_cast<uint32_t *>(hb.data());
        uint64_t out_bytes = K_BYTES > hb.size() ? K_BYTES - hb.size() : 0;

        if (steps.find("read") != std::string::npos) {
            std::printf("\nREAD: random data in the buffer; the kernel XORs 512 MiB from the buffer's card address\n");
            std::mt19937_64 rng(0x5EED ^ (uint64_t) getpid());
            for (uint64_t i = 0; i < hb.size() / 8; i++) {
                reinterpret_cast<uint64_t *>(w)[i] = rng();
            }
            uint32_t exp = xor_low32(w, hb.size());
            uint32_t f0 = d.getHostMemInfo().faultCount;
            double dt = k.run(hb.cardAddr(), 1);
            uint32_t acc = k.rd(K_ACC);
            vrtd::HostMemInfo fi = d.getHostMemInfo();
            check("kernel read: out_acc = XOR of the buffer (the rest reads as zeros)", acc == exp,
                  fmt("0x%08x, expected 0x%08x, %.0f ms", acc, exp, dt * 1e3));
            check("kernel read: everything past the buffer faulted",
                  fi.faultCount - f0 == out_bytes / BURST,
                  fmt("%u faults, expected %" PRIu64, fi.faultCount - f0, out_bytes / BURST));
        }

        if (steps.find("write") != std::string::npos) {
            std::printf("\nWRITE: the kernel writes word i = i over 512 MiB from the buffer's card address\n");
            std::memset(w, 0xCC, hb.size());
            uint32_t f0 = d.getHostMemInfo().faultCount;
            double dt = k.run(hb.cardAddr(), 0);
            uint64_t bad = 0, first_bad = 0;
            for (uint64_t i = 0; i < hb.size() / 32; i++) {
                const uint32_t *word = w + 8 * i;
                bool ok = word[0] == (uint32_t) i;
                for (int j = 1; j < 8 && ok; j++) {
                    ok = word[j] == 0;
                }
                if (!ok && bad++ == 0) {
                    first_bad = i;
                }
            }
            vrtd::HostMemInfo fi = d.getHostMemInfo();
            check("kernel write: every word of the buffer is its own index", bad == 0,
                  bad ? fmt("%" PRIu64 " wrong words, first %" PRIu64 " reads 0x%08x", bad, first_bad,
                            w[8 * first_bad])
                      : fmt("%" PRIu64 " words exact, %.0f ms", hb.size() / 32, dt * 1e3));
            check("kernel write: everything past the buffer faulted",
                  fi.faultCount - f0 == out_bytes / BURST,
                  fmt("%u faults, expected %" PRIu64, fi.faultCount - f0, out_bytes / BURST));
        }

        // 5) close: the card can no longer reach the memory
        uint64_t addr = hb.cardAddr();
        hb.close();
        vrtd::HostMemInfo info3 = d.getHostMemInfo();
        check("host buffer closed, windows free again", info3.windowsFree == info.windowsFree,
              fmt("windows free %u", info3.windowsFree));
        uint32_t f0 = info3.faultCount;
        k.run(addr, 1);
        uint32_t acc = k.rd(K_ACC);
        vrtd::HostMemInfo fi = d.getHostMemInfo();
        check("after close: the kernel reads only zeros, every access faults",
              acc == 0 && fi.faultCount - f0 == K_BYTES / BURST,
              fmt("out_acc 0x%08x, %u faults", acc, fi.faultCount - f0));
    } catch (const vrtd::Error &e) {
        check("vrtd request", false, fmt("%s (code %d)", e.what(), (int) e.getErrorCode()));
    } catch (const std::exception &e) {
        check("run", false, e.what());
    }

    std::printf("\n==== summary: %d pass, %d fail\n", passes, fails);
    return fails ? 1 : 0;
}
