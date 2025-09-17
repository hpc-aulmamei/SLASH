/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
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

#ifndef VRTD_DEVICE_HPP
#define VRTD_DEVICE_HPP

#include <string>
#include <string_view>
#include <functional>
#include <stddef.h>
#include <stdint.h>

#include <vrtd/bar.hpp>

namespace vrtd {

/**
 * @brief Value-type handle describing a vrtd device.
 *
 * A @c Device carries its device number and name and routes operations back
 * through its originating @c Session.
 *
 * @par Lifetime
 * A @c Device becomes invalid if its originating @c Session is closed or moved.
 * Any subsequent member call will throw.
 *
 * @par Thread safety
 * Methods are thread-safe and may be called concurrently; they synchronize
 * on the originating @c Session.
 */
class Device {
public:
    ~Device() = default;

    Device(const Device&)                = default;
    Device& operator=(const Device&)     = default;
    Device(Device&&) noexcept            = default;
    Device& operator=(Device&&) noexcept = default;

    /**
     * @brief Zero-based device index as seen by vrtd.
     */
    uint32_t getNum() const noexcept;

    /**
     * @brief Human-readable device name.
     *
     * Stable for the lifetime of the @c Device object.
     */
    const std::string& getName() const noexcept;

    /**
     * @brief Access a device BAR by index.
     *
     * @param num BAR index.
     * @return Metadata handle for the requested BAR.
     * @throws vrtd::Error on error (e.g., invalid index or unusable session).
     *
     * @par Notes
     * The returned @c Bar becomes invalid if the originating @c Session is
     * later closed or moved.
     */
    Bar getBar(uint8_t num) const;
private:
    // Only allow the Session class to generate this class
    friend class Session;
    Device(uint32_t num, std::string_view name, std::function<Bar(const Device&, uint8_t)> fGetBar);

    uint32_t num;
    std::string name;

    std::function<Bar(const Device&, uint8_t)> fGetBar;
};

}

#endif // VRTD_DEVICE_HPP
