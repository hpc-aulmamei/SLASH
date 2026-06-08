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

/// @file validate.cpp
/// @brief Implementation of the Validate command.
///
/// Resets a V80 board, then exercises HBM and DDR memory over PCIe:
///   1. Data integrity checks (write pattern, read back, verify).
///   2. Parallel bandwidth measurements (N threads, one per buffer).
///
/// We use libvrtdpp (vrtd::Session / vrtd::Device / vrtd::Buffer) directly
/// rather than the higher-level vrt::Device because vrt::Device requires a
/// vrtbin path for system-map parsing, which is unnecessary for raw memory
/// validation.
///
/// TODO: Decide whether vrt::Device should gain a vrtbin-less constructor so
///       that commands like validate can go through the standard vrt:: layer.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "validate.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <vrtd/session.hpp>

#include "bdf.hpp"
#include "raw_transfer.hpp"

#ifdef SMI_ENABLE_QDMA_DRIVER_BACKEND
#include "qdma_driver_backend.hpp"
#endif

extern "C" {
#include <slash/qdma.h>
}

namespace {

using smi::raw::throwSystemError;

/// Buffer size for each allocation (512 MB — one allocator region).
static constexpr uint64_t BUFFER_SIZE = 512ULL * 1024 * 1024;

/// Region constants mirror vrt/vrtd/src/allocator.h, which is private.
static constexpr uint64_t HBM_BASE = 0x4000000000ULL;
static constexpr uint64_t DDR_BASE = 0x60000000000ULL;
static constexpr uint64_t MEM_REGION_SIZE = 512ULL * 1024 * 1024;

static constexpr uint32_t QDMA_Q_MODE_MM = 0;
static constexpr uint32_t QDMA_DIR_H2C = 0x1;
static constexpr uint32_t QDMA_DIR_C2H = 0x2;
static constexpr uint32_t QDMA_RING_SZ_IDX = 0;

static bool checkHostMemoryBudget(const Validate::Options& options) {
    const uint64_t maxConcurrentBuffers = (!options.ddrOnly && !options.hbmOnly)
        ? 4ULL * options.threads
        : 2ULL * options.threads;
    const uint64_t requiredBytes = maxConcurrentBuffers * BUFFER_SIZE;
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long availablePages = sysconf(_SC_AVPHYS_PAGES);

    if (pageSize <= 0 || availablePages <= 0) {
        std::cerr << "Warning: unable to estimate available host memory for validate; "
                  << "peak mapped buffer footprint is "
                  << (requiredBytes / (1024ULL * 1024ULL)) << " MiB." << std::endl;
        return true;
    }

    const auto availableBytes = static_cast<uint64_t>(pageSize) *
        static_cast<uint64_t>(availablePages);
    if (requiredBytes > availableBytes) {
        std::cerr << "validate: requested test can map up to "
                  << (requiredBytes / (1024ULL * 1024ULL)) << " MiB of host buffers, "
                  << "but only about " << (availableBytes / (1024ULL * 1024ULL))
                  << " MiB is currently available. Reduce --threads or use --ddr-only/--hbm-only."
                  << std::endl;
        return false;
    }

    return true;
}

static void warnIfNotRoot(const char* mode) {
    if (geteuid() != 0) {
        std::cerr << "Warning: " << mode
                  << " usually needs root or udev-granted access to QDMA device nodes/sysfs."
                  << std::endl;
    }
}

std::string readDevNameFromUevent(const std::filesystem::path& miscPath) {
    std::ifstream uevent(miscPath / "uevent");
    if (!uevent.is_open()) {
        throw std::runtime_error("Failed to open " + (miscPath / "uevent").string());
    }

    std::string line;
    while (std::getline(uevent, line)) {
        static constexpr std::string_view key{"DEVNAME="};
        if (!line.starts_with(key)) {
            continue;
        }

        std::string devName = line.substr(key.size());
        while (!devName.empty() && (devName.back() == '\n' || devName.back() == '\r')) {
            devName.pop_back();
        }
        return "/dev/" + devName;
    }

    throw std::runtime_error("No DEVNAME entry found in " + (miscPath / "uevent").string());
}

std::string resolveQdmaDevicePath(const std::string& boardBdf) {
    static const std::filesystem::path MISC_PATH{"/sys/class/misc"};

    const std::string exactName = "slash_qdma_ctl_" + boardBdf + ".1";
    const auto exactPath = MISC_PATH / exactName;
    if (std::filesystem::exists(exactPath)) {
        return readDevNameFromUevent(exactPath);
    }

    const std::string prefix = "slash_qdma_ctl_" + boardBdf + ".";
    std::vector<std::filesystem::path> matches;
    for (const auto& entry : std::filesystem::directory_iterator(MISC_PATH)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with(prefix)) {
            matches.push_back(entry.path());
        }
    }

    if (matches.empty()) {
        throw std::runtime_error(
            "No QDMA misc device found for board " + boardBdf +
            " (looked for /sys/class/misc/" + prefix + "*)");
    }

    std::sort(matches.begin(), matches.end());
    if (matches.size() > 1) {
        std::cerr << "Warning: multiple QDMA devices found for " << boardBdf
                  << "; using " << matches.front().filename().string() << std::endl;
    }

    return readDevNameFromUevent(matches.front());
}

class RawQdmaDevice {
public:
    explicit RawQdmaDevice(const std::string& path) : qdma_{slash_qdma_open(path.c_str())} {
        if (qdma_ == nullptr) {
            throwSystemError("Failed to open QDMA device " + path);
        }
    }

    RawQdmaDevice(const RawQdmaDevice&) = delete;
    RawQdmaDevice& operator=(const RawQdmaDevice&) = delete;

    RawQdmaDevice(RawQdmaDevice&& other) noexcept : qdma_{other.qdma_} {
        other.qdma_ = nullptr;
    }

    RawQdmaDevice& operator=(RawQdmaDevice&& other) noexcept {
        if (this != &other) {
            cleanup();
            qdma_ = other.qdma_;
            other.qdma_ = nullptr;
        }
        return *this;
    }

    ~RawQdmaDevice() {
        cleanup();
    }

    slash_qdma* get() const {
        return qdma_;
    }

private:
    void cleanup() {
        if (qdma_ != nullptr) {
            (void)slash_qdma_close(qdma_);
            qdma_ = nullptr;
        }
    }

    slash_qdma* qdma_ = nullptr;
};

class RawTransferBuffer {
public:
    RawTransferBuffer(slash_qdma* qdma, uint64_t physAddr, uint64_t size)
        : qdma_{qdma}, physAddr_{physAddr}, size_{size} {
        try {
            createHostMapping();
            createQpair();
        } catch (...) {
            cleanup();
            throw;
        }
    }

    RawTransferBuffer(const RawTransferBuffer&) = delete;
    RawTransferBuffer& operator=(const RawTransferBuffer&) = delete;

    RawTransferBuffer(RawTransferBuffer&& other) noexcept {
        moveFrom(other);
    }

    RawTransferBuffer& operator=(RawTransferBuffer&& other) noexcept {
        if (this != &other) {
            cleanup();
            moveFrom(other);
        }
        return *this;
    }

    ~RawTransferBuffer() {
        cleanup();
    }

    void* data() {
        return data_;
    }

    uint64_t getSize() const {
        return size_;
    }

    void syncToDevice(uint64_t offset, uint64_t size) {
        validateSyncRange(offset, size);
        transfer(offset, size, /*toDevice=*/true);
    }

    void syncFromDevice(uint64_t offset, uint64_t size) {
        validateSyncRange(offset, size);
        transfer(offset, size, /*toDevice=*/false);
    }

private:
    void moveFrom(RawTransferBuffer& other) noexcept {
        qdma_ = other.qdma_;
        fd_ = other.fd_;
        qid_ = other.qid_;
        qpairCreated_ = other.qpairCreated_;
        qpairStarted_ = other.qpairStarted_;
        data_ = other.data_;
        physAddr_ = other.physAddr_;
        size_ = other.size_;
        transferStepSize_ = other.transferStepSize_;

        other.qdma_ = nullptr;
        other.fd_ = -1;
        other.qid_ = 0;
        other.qpairCreated_ = false;
        other.qpairStarted_ = false;
        other.data_ = nullptr;
        other.physAddr_ = 0;
        other.size_ = 0;
        other.transferStepSize_ = 0;
    }

    void createHostMapping() {
        smi::raw::HostMapping mapping = smi::raw::createHostMapping(size_, physAddr_);
        data_ = mapping.data;
        transferStepSize_ = mapping.step;
    }

    void createQpair() {
        if (qdma_ == nullptr || size_ == 0) {
            throw std::invalid_argument("Invalid raw transfer buffer arguments");
        }

        struct slash_qdma_qpair_add req{};
        req.size = sizeof(req);
        req.mode = QDMA_Q_MODE_MM;
        req.dir_mask = QDMA_DIR_H2C | QDMA_DIR_C2H;
        req.h2c_ring_sz = QDMA_RING_SZ_IDX;
        req.c2h_ring_sz = QDMA_RING_SZ_IDX;
        req.cmpt_ring_sz = QDMA_RING_SZ_IDX;

        if (slash_qdma_qpair_add(qdma_, &req) != 0) {
            throwSystemError("Failed to add raw transfer QDMA queue pair");
        }
        qid_ = req.qid;
        qpairCreated_ = true;

        if (slash_qdma_qpair_start(qdma_, qid_) != 0) {
            throwSystemError("Failed to start raw transfer QDMA queue pair");
        }
        qpairStarted_ = true;

        fd_ = slash_qdma_qpair_get_fd(qdma_, qid_, O_CLOEXEC);
        if (fd_ < 0) {
            throwSystemError("Failed to get raw transfer QDMA queue fd");
        }
    }

    void validateSyncRange(uint64_t offset, uint64_t size) const {
        smi::raw::validateSyncRange(offset, size, size_, physAddr_, transferStepSize_);
    }

    void transfer(uint64_t offset, uint64_t size, bool toDevice) {
        smi::raw::rawTransfer(fd_, data_, physAddr_, offset, size, transferStepSize_, toDevice);
    }

    void cleanup() {
        if (fd_ >= 0) {
            (void)close(fd_);
            fd_ = -1;
        }
        if (qdma_ != nullptr && qpairStarted_) {
            (void)slash_qdma_qpair_stop(qdma_, qid_);
            qpairStarted_ = false;
        }
        if (qdma_ != nullptr && qpairCreated_) {
            (void)slash_qdma_qpair_del(qdma_, qid_);
            qpairCreated_ = false;
        }
        if (data_ != nullptr && data_ != MAP_FAILED) {
            (void)munmap(data_, size_);
            data_ = nullptr;
        }
    }

    slash_qdma* qdma_ = nullptr;
    int fd_ = -1;
    uint32_t qid_ = 0;
    bool qpairCreated_ = false;
    bool qpairStarted_ = false;
    void* data_ = nullptr;
    uint64_t physAddr_ = 0;
    uint64_t size_ = 0;
    uint64_t transferStepSize_ = 0;
};

/// Fill @p buf with a deterministic pattern seeded by @p seed.
static void fillPattern(void* buf, uint64_t size, uint32_t seed) {
    auto* p = static_cast<uint32_t*>(buf);
    uint64_t count = size / sizeof(uint32_t);
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = static_cast<uint32_t>(i) ^ seed;
    }
}

/// Verify @p buf matches the pattern produced by fillPattern().
/// Returns true on match, false on first mismatch.
static bool verifyPattern(const void* buf, uint64_t size, uint32_t seed) {
    auto* p = static_cast<const uint32_t*>(buf);
    uint64_t count = size / sizeof(uint32_t);
    for (uint64_t i = 0; i < count; ++i) {
        if (p[i] != (static_cast<uint32_t>(i) ^ seed)) {
            return false;
        }
    }
    return true;
}

/// Run data integrity on every buffer: write pattern → sync to device →
/// clear host → sync from device → verify.
///
/// Output policy: per-buffer FAIL lines are printed as failures occur; OK
/// buffers are silent.  A single summary line ("N/N OK" or "M/N OK, K
/// FAIL") is printed at the end.
///
/// @return true if all buffers pass.
template<typename Buffer>
static bool testDataIntegrity(std::vector<Buffer>& buffers,
                              const std::string& label) {
    const size_t total = buffers.size();
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        auto& buf = buffers[i];
        uint32_t seed = static_cast<uint32_t>(i);
        uint64_t size = buf.getSize();

        fillPattern(buf.data(), size, seed);
        buf.syncToDevice(0, size);

        std::memset(buf.data(), 0, size);
        buf.syncFromDevice(0, size);

        if (verifyPattern(buf.data(), size, seed)) {
            ++passed;
        } else {
            std::cout << "    " << label << i << ": FAIL" << std::endl;
        }
    }

    if (passed == total) {
        std::cout << "    " << total << "/" << total << " OK" << std::endl;
    } else {
        std::cout << "    " << passed << "/" << total << " OK, "
                  << (total - passed) << " FAIL" << std::endl;
    }

    return passed == total;
}

static double mbPerSecond(uint64_t bytes, std::chrono::duration<double> elapsed) {
    const double totalMB = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return totalMB / elapsed.count();
}

static void printBandwidthMetric(const char* label, double mbps) {
    std::cout << "    " << label << ": " << std::fixed << std::setprecision(2)
              << mbps << " MB/s" << std::endl;
}

template<typename Buffer>
static uint64_t fillBuffers(std::vector<Buffer>& buffers, int value) {
    uint64_t totalBytes = 0;
    for (auto& buf : buffers) {
        std::memset(buf.data(), value, buf.getSize());
        totalBytes += buf.getSize();
    }
    return totalBytes;
}

template<typename Buffer>
static void launchTransferThreads(std::vector<Buffer>& buffers,
                                  bool toDevice,
                                  std::vector<std::thread>& threads,
                                  std::vector<std::exception_ptr>& errors,
                                  size_t errorOffset) {
    for (size_t i = 0; i < buffers.size(); ++i) {
        threads.emplace_back([&buffers, &errors, i, errorOffset, toDevice] {
            try {
                if (toDevice) {
                    buffers[i].syncToDevice(0, buffers[i].getSize());
                } else {
                    buffers[i].syncFromDevice(0, buffers[i].getSize());
                }
            } catch (...) {
                errors[errorOffset + i] = std::current_exception();
            }
        });
    }
}

template<typename Buffer>
static void runTransfers(std::vector<Buffer>& buffers, bool toDevice) {
    std::vector<std::thread> threads;
    std::vector<std::exception_ptr> errors(buffers.size());
    threads.reserve(buffers.size());

    launchTransferThreads(buffers, toDevice, threads, errors, 0);

    for (auto& t : threads) {
        t.join();
    }
    for (auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

template<typename Buffer>
static double testSingleDirectionBandwidth(std::vector<Buffer>& buffers, bool toDevice) {
    const uint64_t totalBytes = fillBuffers(buffers, toDevice ? 0xAB : 0xCD);

    if (!toDevice) {
        runTransfers(buffers, /*toDevice=*/true);
        for (auto& buf : buffers) {
            std::memset(buf.data(), 0, buf.getSize());
        }
    }

    const auto start = std::chrono::steady_clock::now();
    runTransfers(buffers, toDevice);
    const auto end = std::chrono::steady_clock::now();

    return mbPerSecond(totalBytes, end - start);
}

template<typename Buffer>
static void testBidirectionalBandwidth(std::vector<Buffer>& writeBuffers,
                                       std::vector<Buffer>& readBuffers) {
    const uint64_t writeBytes = fillBuffers(writeBuffers, 0xAB);
    const uint64_t readBytes = fillBuffers(readBuffers, 0xCD);

    // Prime device memory before timing so the C2H side reads initialized data.
    runTransfers(readBuffers, /*toDevice=*/true);
    for (auto& buf : readBuffers) {
        std::memset(buf.data(), 0, buf.getSize());
    }

    std::vector<std::thread> threads;
    std::vector<std::exception_ptr> errors(writeBuffers.size() + readBuffers.size());
    threads.reserve(errors.size());

    const auto start = std::chrono::steady_clock::now();
    launchTransferThreads(writeBuffers, /*toDevice=*/true, threads, errors, 0);
    launchTransferThreads(readBuffers, /*toDevice=*/false, threads, errors, writeBuffers.size());

    for (auto& t : threads) {
        t.join();
    }
    const auto end = std::chrono::steady_clock::now();

    for (auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    const auto elapsed = end - start;
    const double writeMBps = mbPerSecond(writeBytes, elapsed);
    const double readMBps = mbPerSecond(readBytes, elapsed);

    printBandwidthMetric("Read", readMBps);
    printBandwidthMetric("Write", writeMBps);
    printBandwidthMetric("Total", readMBps + writeMBps);
}

template<typename Buffer>
static void testBandwidthSuite(std::vector<Buffer>& singleDirectionBuffers,
                               const std::string& label,
                               const std::string& backendSuffix) {
    std::cout << "Testing " << label << " read bandwidth ("
              << singleDirectionBuffers.size() << " threads" << backendSuffix << ")..." << std::endl;
    printBandwidthMetric("Read", testSingleDirectionBandwidth(singleDirectionBuffers, /*toDevice=*/false));

    std::cout << "Testing " << label << " write bandwidth ("
              << singleDirectionBuffers.size() << " threads" << backendSuffix << ")..." << std::endl;
    printBandwidthMetric("Write", testSingleDirectionBandwidth(singleDirectionBuffers, /*toDevice=*/true));
}

template<typename Buffer>
static void testBidirectionalBandwidthSuite(std::vector<Buffer>& bidirectionalWriteBuffers,
                                            std::vector<Buffer>& bidirectionalReadBuffers,
                                            const std::string& label,
                                            const std::string& backendSuffix) {
    std::cout << "Testing " << label << " bidirectional bandwidth ("
              << (bidirectionalWriteBuffers.size() + bidirectionalReadBuffers.size())
              << " threads" << backendSuffix << ")..." << std::endl;
    testBidirectionalBandwidth(bidirectionalWriteBuffers, bidirectionalReadBuffers);
}

static int runRawTransferTest(const std::string& bdf, const Validate::Options& options) {
    const unsigned N = options.threads;

    if (!options.noReset) {
        std::cout << "Raw transfer mode skips reset; continuing without VRTD reset." << std::endl;
    }
    warnIfNotRoot("SLASH raw transfer mode");

    const std::string qdmaPath = resolveQdmaDevicePath(bdf);
    std::cout << "Using raw QDMA device " << qdmaPath << "..." << std::endl;

    RawQdmaDevice qdma(qdmaPath);

    if (!options.ddrOnly) {
        std::cout << "Testing HBM data integrity (" << N << " regions, raw QDMA)..." << std::endl;
        {
            std::vector<RawTransferBuffer> hbmBuffers;
            hbmBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmBuffers.emplace_back(qdma.get(), HBM_BASE + i * MEM_REGION_SIZE,
                                        BUFFER_SIZE);
            }

            if (!testDataIntegrity(hbmBuffers, "HBM")) {
                std::cerr << "HBM data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(hbmBuffers, "HBM", ", raw QDMA");
        }
        {
            // Bidirectional HBM: positions interleave R/W across regions
            // 0..2N-1.  Reads land on even regions, writes on odd regions.
            std::vector<RawTransferBuffer> hbmWriteBuffers;
            std::vector<RawTransferBuffer> hbmReadBuffers;
            hbmWriteBuffers.reserve(N);
            hbmReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmReadBuffers.emplace_back(qdma.get(),
                                            HBM_BASE + (2 * i) * MEM_REGION_SIZE,
                                            BUFFER_SIZE);
                hbmWriteBuffers.emplace_back(qdma.get(),
                                             HBM_BASE + (2 * i + 1) * MEM_REGION_SIZE,
                                             BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(hbmWriteBuffers, hbmReadBuffers, "HBM", ", raw QDMA");
        }
    }

    if (!options.hbmOnly) {
        std::cout << "Testing DDR data integrity (" << N << " buffers, raw QDMA)..." << std::endl;
        {
            std::vector<RawTransferBuffer> ddrBuffers;
            ddrBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrBuffers.emplace_back(qdma.get(), DDR_BASE + i * BUFFER_SIZE,
                                        BUFFER_SIZE);
            }

            if (!testDataIntegrity(ddrBuffers, "DDR")) {
                std::cerr << "DDR data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(ddrBuffers, "DDR", ", raw QDMA");
        }
        {
            // Bidirectional DDR: positions interleave R/W across slot indices
            // 0..2N-1 of the DDR address space.
            std::vector<RawTransferBuffer> ddrWriteBuffers;
            std::vector<RawTransferBuffer> ddrReadBuffers;
            ddrWriteBuffers.reserve(N);
            ddrReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrReadBuffers.emplace_back(qdma.get(),
                                            DDR_BASE + (2 * i) * BUFFER_SIZE,
                                            BUFFER_SIZE);
                ddrWriteBuffers.emplace_back(qdma.get(),
                                             DDR_BASE + (2 * i + 1) * BUFFER_SIZE,
                                             BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(ddrWriteBuffers, ddrReadBuffers, "DDR", ", raw QDMA");
        }
    }

    if (!options.ddrOnly && !options.hbmOnly) {
        {
            std::vector<RawTransferBuffer> parBuffers;
            parBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.emplace_back(qdma.get(), HBM_BASE + i * MEM_REGION_SIZE,
                                        BUFFER_SIZE);
            }
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.emplace_back(qdma.get(), DDR_BASE + i * BUFFER_SIZE,
                                        BUFFER_SIZE);
            }

            testBandwidthSuite(parBuffers, "HBM+DDR", ", raw QDMA");
        }
        {
            // Bidirectional HBM+DDR: 4N positions total.  Positions 0..2N-1
            // are HBM (interleaved R/W across regions 0..2N-1); positions
            // 2N..4N-1 are DDR (interleaved R/W across DDR slots 0..2N-1).
            // Channel = (p / 2) & 1 throughout.
            std::vector<RawTransferBuffer> parWriteBuffers;
            std::vector<RawTransferBuffer> parReadBuffers;
            parWriteBuffers.reserve(2 * N);
            parReadBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parReadBuffers.emplace_back(qdma.get(),
                                            HBM_BASE + (2 * i) * MEM_REGION_SIZE,
                                            BUFFER_SIZE);
                parWriteBuffers.emplace_back(qdma.get(),
                                             HBM_BASE + (2 * i + 1) * MEM_REGION_SIZE,
                                             BUFFER_SIZE);
            }
            for (unsigned i = 0; i < N; ++i) {
                parReadBuffers.emplace_back(qdma.get(),
                                            DDR_BASE + (2 * i) * BUFFER_SIZE,
                                            BUFFER_SIZE);
                parWriteBuffers.emplace_back(qdma.get(),
                                             DDR_BASE + (2 * i + 1) * BUFFER_SIZE,
                                             BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(parWriteBuffers, parReadBuffers, "HBM+DDR", ", raw QDMA");
        }
    }

    return 0;
}

/// Raw integrity + bandwidth test driven over the off-the-shelf Xilinx QDMA
/// driver instead of SLASH.  smi provisions queues itself (qmax + netlink
/// add/start) and transfers over the per-queue char devices.
static int runQdmaDriverTest(const std::string& bdf, const Validate::Options& options) {
#ifndef SMI_ENABLE_QDMA_DRIVER_BACKEND
    (void)bdf;
    (void)options;
    std::cerr << "validate: this v80-smi build was configured without "
              << "--use-qdma-driver support. Rebuild with "
              << "-DSMI_ENABLE_QDMA_DRIVER_BACKEND=ON." << std::endl;
    return 1;
#else
    const unsigned N = options.threads;

    if (!options.noReset) {
        std::cout << "QDMA-driver raw mode skips reset; continuing without VRTD reset." << std::endl;
    }
    warnIfNotRoot("QDMA-driver raw mode");

    const bool runParallel = !options.ddrOnly && !options.hbmOnly;

    std::cout << "Using off-the-shelf Xilinx QDMA driver for board " << bdf << "..." << std::endl;
    smi::qdma_driver::QdmaDriverDevice qdma(bdf);
    std::cout << "Resolved QDMA function " << qdma.functionBdf() << std::endl;
    qdma.ensureQmax(runParallel ? 4 * N : 2 * N);

    const unsigned mmChannels = qdma.mmChannelMax();
    if (mmChannels > 1) {
        std::cout << "Distributing queues across " << mmChannels
                  << " MM channels (channel = qid % " << mmChannels << ")." << std::endl;
    } else {
        std::cout << "Device exposes a single MM channel; all queues on channel 0." << std::endl;
    }

    if (!options.ddrOnly) {
        std::cout << "Testing HBM data integrity (" << N << " regions, QDMA driver)..." << std::endl;
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> hbmBuffers;
            hbmBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmBuffers.emplace_back(qdma, i, HBM_BASE + i * MEM_REGION_SIZE, BUFFER_SIZE);
            }

            if (!testDataIntegrity(hbmBuffers, "HBM")) {
                std::cerr << "HBM data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(hbmBuffers, "HBM", ", QDMA driver");
        }
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> hbmWriteBuffers;
            std::vector<smi::qdma_driver::QdmaDriverBuffer> hbmReadBuffers;
            hbmWriteBuffers.reserve(N);
            hbmReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmReadBuffers.emplace_back(qdma, i,
                                            HBM_BASE + (2 * i) * MEM_REGION_SIZE, BUFFER_SIZE);
                hbmWriteBuffers.emplace_back(qdma, N + i,
                                             HBM_BASE + (2 * i + 1) * MEM_REGION_SIZE, BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(hbmWriteBuffers, hbmReadBuffers, "HBM", ", QDMA driver");
        }
    }

    if (!options.hbmOnly) {
        std::cout << "Testing DDR data integrity (" << N << " buffers, QDMA driver)..." << std::endl;
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> ddrBuffers;
            ddrBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrBuffers.emplace_back(qdma, i, DDR_BASE + i * BUFFER_SIZE, BUFFER_SIZE);
            }

            if (!testDataIntegrity(ddrBuffers, "DDR")) {
                std::cerr << "DDR data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(ddrBuffers, "DDR", ", QDMA driver");
        }
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> ddrWriteBuffers;
            std::vector<smi::qdma_driver::QdmaDriverBuffer> ddrReadBuffers;
            ddrWriteBuffers.reserve(N);
            ddrReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrReadBuffers.emplace_back(qdma, i,
                                            DDR_BASE + (2 * i) * BUFFER_SIZE, BUFFER_SIZE);
                ddrWriteBuffers.emplace_back(qdma, N + i,
                                             DDR_BASE + (2 * i + 1) * BUFFER_SIZE, BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(ddrWriteBuffers, ddrReadBuffers, "DDR", ", QDMA driver");
        }
    }

    if (runParallel) {
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> parBuffers;
            parBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.emplace_back(qdma, i, HBM_BASE + i * MEM_REGION_SIZE, BUFFER_SIZE);
            }
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.emplace_back(qdma, N + i, DDR_BASE + i * BUFFER_SIZE, BUFFER_SIZE);
            }

            testBandwidthSuite(parBuffers, "HBM+DDR", ", QDMA driver");
        }
        {
            std::vector<smi::qdma_driver::QdmaDriverBuffer> parWriteBuffers;
            std::vector<smi::qdma_driver::QdmaDriverBuffer> parReadBuffers;
            parWriteBuffers.reserve(2 * N);
            parReadBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parReadBuffers.emplace_back(qdma, i,
                                            HBM_BASE + (2 * i) * MEM_REGION_SIZE, BUFFER_SIZE);
                parWriteBuffers.emplace_back(qdma, 2 * N + i,
                                             HBM_BASE + (2 * i + 1) * MEM_REGION_SIZE, BUFFER_SIZE);
            }
            for (unsigned i = 0; i < N; ++i) {
                parReadBuffers.emplace_back(qdma, N + i,
                                            DDR_BASE + (2 * i) * BUFFER_SIZE, BUFFER_SIZE);
                parWriteBuffers.emplace_back(qdma, 3 * N + i,
                                             DDR_BASE + (2 * i + 1) * BUFFER_SIZE, BUFFER_SIZE);
            }

            testBidirectionalBandwidthSuite(parWriteBuffers, parReadBuffers, "HBM+DDR", ", QDMA driver");
        }
    }

    return 0;
#endif
}

} // namespace

int Validate::run(const Options& options) {
    std::string bdf = resolveBoardBdf(options.bdf, "validate");
    unsigned N = options.threads;

    if (!checkHostMemoryBudget(options)) {
        return 1;
    }

    // The HBM bidirectional phase uses 2*N HBM regions (write 0..N-1, read N..2N-1).
    // HBM has only 64 regions, so N>32 is unsupportable unless HBM is excluded.
    static constexpr unsigned HBM_REGIONS = 64;
    if (!options.ddrOnly && 2 * N > HBM_REGIONS) {
        std::cerr << "validate: --threads > " << (HBM_REGIONS / 2)
                  << " requires --ddr-only (bidirectional HBM uses 2*N HBM regions, only "
                  << HBM_REGIONS << " exist)" << std::endl;
        return 1;
    }

    if (options.rawTransferTest) {
        return runRawTransferTest(bdf, options);
    }

    if (options.useQdmaDriver) {
        return runQdmaDriverTest(bdf, options);
    }

    // -- Step 1: (Optional) Reset the device via vrtd --
    if (!options.noReset) {
        std::cout << "Resetting device " << bdf << "..." << std::endl;
        {
            vrtd::Session session;
            auto device = session.getDeviceByBdf(bdf);
            device.hotplugOp(vrtd::HotplugOp::ResetSequence);
        }
        // Session is torn down; the daemon has re-discovered the device.
    }

    vrtd::Session session;
    auto device = session.getDeviceByBdf(bdf);

    // -- Step 2: HBM — integrity then bandwidth --
    if (!options.ddrOnly) {
        std::cout << "Testing HBM data integrity (" << N << " regions)..." << std::endl;
        {
            std::vector<vrtd::Buffer> hbmBuffers;
            hbmBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmBuffers.push_back(device.openHbmBuffer(i, BUFFER_SIZE));
            }

            if (!testDataIntegrity(hbmBuffers, "HBM")) {
                std::cerr << "HBM data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(hbmBuffers, "HBM", "");
        }
        // HBM buffers released.
        {
            std::vector<vrtd::Buffer> hbmWriteBuffers;
            std::vector<vrtd::Buffer> hbmReadBuffers;
            hbmWriteBuffers.reserve(N);
            hbmReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                hbmReadBuffers.push_back(device.openHbmBuffer(2 * i, BUFFER_SIZE));
                hbmWriteBuffers.push_back(device.openHbmBuffer(2 * i + 1, BUFFER_SIZE));
            }

            testBidirectionalBandwidthSuite(hbmWriteBuffers, hbmReadBuffers, "HBM", "");
        }
        // Bidirectional HBM buffers released.
    }

    // -- Step 3: DDR — integrity then bandwidth --
    if (!options.hbmOnly) {
        std::cout << "Testing DDR data integrity (" << N << " buffers)..." << std::endl;
        {
            std::vector<vrtd::Buffer> ddrBuffers;
            ddrBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
            }

            if (!testDataIntegrity(ddrBuffers, "DDR")) {
                std::cerr << "DDR data integrity check failed" << std::endl;
                return 1;
            }

            testBandwidthSuite(ddrBuffers, "DDR", "");
        }
        // DDR buffers released.
        {
            std::vector<vrtd::Buffer> ddrWriteBuffers;
            std::vector<vrtd::Buffer> ddrReadBuffers;
            ddrWriteBuffers.reserve(N);
            ddrReadBuffers.reserve(N);
            for (unsigned i = 0; i < N; ++i) {
                ddrWriteBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
                ddrReadBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
            }

            testBidirectionalBandwidthSuite(ddrWriteBuffers, ddrReadBuffers, "DDR", "");
        }
        // Bidirectional DDR buffers released.
    }

    // -- Step 4: HBM + DDR in parallel --
    if (!options.ddrOnly && !options.hbmOnly) {
        {
            std::vector<vrtd::Buffer> parBuffers;
            parBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.push_back(device.openHbmBuffer(i, BUFFER_SIZE));
            }
            for (unsigned i = 0; i < N; ++i) {
                parBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
            }

            testBandwidthSuite(parBuffers, "HBM+DDR", "");
        }
        // Parallel single-direction buffers released.
        {
            std::vector<vrtd::Buffer> parWriteBuffers;
            std::vector<vrtd::Buffer> parReadBuffers;
            parWriteBuffers.reserve(2 * N);
            parReadBuffers.reserve(2 * N);
            for (unsigned i = 0; i < N; ++i) {
                parReadBuffers.push_back(device.openHbmBuffer(2 * i, BUFFER_SIZE));
                parWriteBuffers.push_back(device.openHbmBuffer(2 * i + 1, BUFFER_SIZE));
            }
            for (unsigned i = 0; i < N; ++i) {
                parWriteBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
                parReadBuffers.push_back(device.openDdrBuffer(BUFFER_SIZE));
            }

            testBidirectionalBandwidthSuite(parWriteBuffers, parReadBuffers, "HBM+DDR", "");
        }
        // Parallel bidirectional buffers released.
    }

    return 0;
}
