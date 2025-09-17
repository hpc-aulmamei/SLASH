#ifndef VRTD_ERROR_H
#define VRTD_ERROR_H

#include <stdexcept>

#include <vrtd/vrtd.h>

namespace vrtd {

/**
 * @brief Exception type for libvrtd/libvrtd++ operations.
 *
 * Wraps a #vrtd_ret code and exposes a human-readable, static message via
 * @c what(). Use @c getErrorCode() to branch on a specific error.
 *
 * @note Transport/socket issues in the C++ layer are mapped to
 *       #VRTD_RET_BAD_CONN.
 * @note The message returned by @c what() is a static string mapped from the
 *       code (e.g., "Authentication error") and does not allocate.
 */
class Error : public std::exception {
private:
    vrtd_ret errorCode;

public:
    /**
     * @brief Construct an Error with the given code.
     * @param errorCode A value from #vrtd_ret.
     */
    explicit Error(vrtd_ret errorCode) noexcept;

    ~Error() = default;

    Error(const Error&)                = default;
    Error& operator=(const Error&)     = default;
    Error(Error&&) noexcept            = default;
    Error& operator=(Error&&) noexcept = default;

    /**
     * @brief Retrieve the underlying error code.
     */
    vrtd_ret getErrorCode() const noexcept;

    /**
     * @brief Human-readable description corresponding to @c errorCode.
     *
     * @return Pointer to a static, null-terminated string. The storage is
     *         valid for the lifetime of the program.
     */
    const char *what() const noexcept override;
};

}

#endif //VRTD_ERROR_H
