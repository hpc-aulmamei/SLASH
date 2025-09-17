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
