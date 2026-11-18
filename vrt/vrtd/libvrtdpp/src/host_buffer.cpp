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

#include <vrtd/host_buffer.hpp>

#include <sys/mman.h>
#include <unistd.h>

#include <utility>

namespace vrtd {

HostBuffer::HostBuffer(const struct vrtd_host_buffer& b, std::function<void(struct vrtd_host_buffer&)> c)
    : buf(b), closer(std::move(c))
{
}

HostBuffer::~HostBuffer()
{
    close();
}

HostBuffer::HostBuffer(HostBuffer&& other) noexcept
    : buf(other.buf), closer(std::move(other.closer))
{
    other.buf = { -1, nullptr, 0, 0, 0 };
}

HostBuffer& HostBuffer::operator=(HostBuffer&& other) noexcept
{
    if (this != &other) {
        close();
        buf = other.buf;
        closer = std::move(other.closer);
        other.buf = { -1, nullptr, 0, 0, 0 };
    }
    return *this;
}

void HostBuffer::close() noexcept
{
    if (isClosed()) {
        return;
    }
    if (closer) {
        closer(buf);          // tells vrtd, unmaps, closes the fd
    }
    if (buf.map != nullptr) {
        (void) munmap(buf.map, buf.size);
    }
    if (buf.fd >= 0) {
        (void) ::close(buf.fd);
    }
    buf = { -1, nullptr, 0, 0, 0 };
}

}
