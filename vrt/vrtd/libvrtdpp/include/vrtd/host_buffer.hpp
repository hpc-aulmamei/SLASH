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

#ifndef VRTD_HOST_BUFFER_HPP
#define VRTD_HOST_BUFFER_HPP

#include <cstdint>
#include <functional>

#include <vrtd/vrtd.h>

namespace vrtd {

/** What the card may do with a host buffer. */
enum class HostBufferPerm : uint32_t {
    Read      = VRTD_HOST_BUFFER_READ,
    Write     = VRTD_HOST_BUFFER_WRITE,
    ReadWrite = VRTD_HOST_BUFFER_READ | VRTD_HOST_BUFFER_WRITE,
};

/** The device's host-memory translator (Device::getHostMemInfo()). */
struct HostMemInfo {
    bool present = false;        ///< Host buffers possible on the loaded design.
    bool iommu = false;          ///< An IOMMU translates host DMA addresses.
    uint32_t windowsTotal = 0;   ///< Slave-bridge windows (one per 2 MiB page).
    uint32_t windowsFree = 0;    ///< Of which free.
    uint32_t pageSize = 0;       ///< Bytes per page.
    uint64_t vaBase = 0;         ///< Card address of the aperture.
    uint64_t vaSize = 0;         ///< Aperture size.
    uint32_t faultCount = 0;     ///< Translator faults since the last clear.
    uint32_t faultInfo = 0;      ///< FAULT_INFO (bit 31 valid, 30 write, 26:24 cause).
    uint64_t faultAddr = 0;      ///< Card address of the first fault.
};

/**
 * @brief Host memory a user-region kernel reaches at cardAddr() (RAII).
 *
 * Kernel ports connected to HOST address [cardAddr(), cardAddr() + size());
 * the CPU sees the same memory at data().  Accesses outside every open host
 * buffer fault in the translator and never leave the card.  Destruction (or
 * close()) makes the memory unreachable for the card again.  The owning
 * vrtd::Session must outlive the buffer.
 */
class HostBuffer {
public:
    HostBuffer() = default;
    ~HostBuffer();

    HostBuffer(const HostBuffer&)            = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    HostBuffer(HostBuffer&& other) noexcept;
    HostBuffer& operator=(HostBuffer&& other) noexcept;

    void *data() const noexcept { return buf.map; }
    uint64_t size() const noexcept { return buf.size; }
    uint64_t cardAddr() const noexcept { return buf.card_addr; }
    uint32_t getDeviceNum() const noexcept { return buf.dev; }
    int getFd() const noexcept { return buf.fd; }
    bool isClosed() const noexcept { return buf.map == nullptr && buf.fd < 0; }

    /** Close the buffer now (idempotent). */
    void close() noexcept;

private:
    friend class Session;
    HostBuffer(const struct vrtd_host_buffer& b, std::function<void(struct vrtd_host_buffer&)> closer);

    struct vrtd_host_buffer buf { -1, nullptr, 0, 0, 0 };
    std::function<void(struct vrtd_host_buffer&)> closer;
};

}

#endif // VRTD_HOST_BUFFER_HPP
