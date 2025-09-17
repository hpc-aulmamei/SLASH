#ifndef VRTD_SESSION_HPP
#define VRTD_SESSION_HPP

#include <stdint.h>
#include <vrtd/vrtd.h>
#include <vrtd/device.hpp>
#include <vrtd/bar.hpp>
#include <vrtd/bar_file.hpp>

#include <mutex>
#include <memory>

namespace vrtd {


/**
 * @brief Owning session/connection to the V Runtime Daemon (vrtd).
 *
 * A @c Session wraps a connected libvrtd socket and provides typed, exception-based
 * access to devices and BARs. All public member functions are thread-safe; calls
 * synchronize on an internal @c std::mutex.
 *
 * @par Exceptions
 * Most member functions throw #vrtd::Error on failure. The destructor never throws.
 *
 * @par Lifetime and moves
 * - The session is non-copyable and movable.
 * - Moving a session leaves the moved-from object in the closed state
 *   (i.e., @c isClosed()==true and @c operator bool() == false).
 * - **Important:** Any @c Device or @c Bar previously obtained from a session becomes
 *   invalid once that session is closed or moved; subsequent operations on those
 *   objects will throw.
 */
class Session {
public:
    /**
     * @brief Construct and connect to the vrtd socket.
     *
     * @param socket_path Filesystem path to the vrtd UNIX socket.
     *                    Defaults to the standard path.
     * @throws vrtd::Error if the connection cannot be established.
     */
    explicit Session(const char *socket_path = VRTD_STANDARD_PATH);

    /**
     * @brief Destructor; closes the session if still open.
     */
    ~Session() noexcept;

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    /**
     * @brief Move-construct a session.
     *
     * The moved-from session becomes closed.
     *
     * @param other The session to move from.
     */
    Session(Session&& other) noexcept;

    /**
     * @brief Move-assign a session.
     *
     * Closes any existing connection, then takes ownership from @p other.
     * The moved-from session becomes closed.
    *
     * @param other The session to move from.
     */
    Session& operator=(Session&& other) noexcept;

    /**
     * @brief Number of devices visible via vrtd.
     * @return Device count.
     * @throws vrtd::Error on error.
     *
     * @par Thread safety
     * Safe for concurrent calls across threads.
     */
    uint32_t getNumDevices() const;

    /**
     * @brief Retrieve a device handle by index.
     *
     * @param i Zero-based device index; must be less than @c getNumDevices().
     * @return A lightweight @c Device value referring back to this session.
     * @throws vrtd::Error if @p i is out of range or if the session is not usable.
     *
     * @par Notes
     * The returned @c Device becomes invalid if this session is later closed or moved.
     */
    Device getDevice(size_t i) const;

    /**
     * @brief Explicitly close the session.
     *
     * Idempotent. After closing, @c isClosed()==true and further operations
     * on this session or on previously obtained @c Device/@c Bar objects will throw.
     */
    void close() noexcept;

    /**
     * @brief Whether the session is closed.
     */
    bool isClosed() const noexcept;

    /**
     * @brief Truthiness conversion.
     *
     * @return @c true if the session is open (not closed).
     */
    explicit operator bool() const noexcept;
private:
    int fd;
    mutable std::unique_ptr<std::mutex> m;

    /**
     * @internal Obtains a BAR for @p device. Called via @c Device::getBar().
     */
    Bar getBar(const Device& device, uint8_t bar_number) const;

    /**
     * @internal Opens and mmaps a BAR file. Called via @c Bar::openBarFile().
     */
    BarFile openBarFile(const Bar &bar) const;
};

}

#endif // VRTD_SESSION_HPP
