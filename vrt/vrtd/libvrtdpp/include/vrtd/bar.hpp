#ifndef VRTD_BAR_HPP
#define VRTD_BAR_HPP

#include <string>
#include <string_view>
#include <functional>
#include <stdint.h>

#include <vrtd/bar_file.hpp>

namespace vrtd {

/**
 * @brief Value-type metadata handle for a device BAR (Base Address Register).
 *
 * Provides discovery information and a convenience method to open/map the BAR.
 *
 * @par Semantics
 * - @c isUsable(): this BAR is currently accessible/mappable to the caller.
 * - @c isInUse(): this BAR is leased by another tenant (currently always false).
 * - @c getStartAddress(), @c getLength(): physical address and size in **bytes**.
 *
 * @par Lifetime
 * A @c Bar becomes invalid if its originating @c Session is closed or moved.
 * Any subsequent member call will throw.
 *
 * @par Thread safety
 * Methods are thread-safe and may be called concurrently; they synchronize
 * on the originating @c Session.
 */
class Bar {
public:
    ~Bar() = default;

    Bar(const Bar&)                = default;
    Bar& operator=(const Bar&)     = default;
    Bar(Bar&&) noexcept            = default;
    Bar& operator=(Bar&&) noexcept = default;

    /**
     * @brief Zero-based device index that owns this BAR.
     */
    uint32_t getDeviceNum() const noexcept;
    
    /**
     * @brief Zero-based BAR index on the device.
     */
    uint8_t getNum() const noexcept;
    
    /**
     * @brief Whether this BAR is currently usable (mappable) by the caller.
     */
    bool isUsable() const noexcept;

    /**
     * @brief Whether this BAR is currently in use by another tenant.
     *
     * @note In the current implementation this always returns false.
     */
    bool isInUse() const noexcept;

    /**
     * @brief Physical start address of the BAR.
     */
    uint64_t getStartAddress() const noexcept;

    /**
     * @brief Length/size of the BAR (bytes).
     */
    uint64_t getLength() const noexcept;

    /**
     * @brief Open and @c mmap() this BAR, returning an owning @c BarFile.
     *
     * @return @c BarFile that RAII-owns the FD and mapping; its destructor
     *         unmaps and closes automatically.
     * @throws vrtd::Error on failure.
     */
    BarFile openBarFile() const;
private:
    // Only allow the Session class to generate this class
    friend class Session;
    Bar(uint32_t deviceNum, uint8_t num, bool usable, bool inUse, uint64_t startAddress, uint64_t length, std::function<BarFile(const Bar&)> fOpenBarFile) noexcept;

    uint32_t deviceNum;
    uint8_t num;
    bool usable;
    bool inUse;
    uint64_t startAddress;
    uint64_t length;

    std::function<BarFile(const Bar&)> fOpenBarFile;
};

} // namespace vrtd

#endif // VRTD_BAR_HPP