#include <vrtd/error.hpp>

namespace vrtd {

Error::Error(vrtd_ret errorCode) noexcept {
    this->errorCode = errorCode;
}

vrtd_ret Error::getErrorCode() const noexcept {
    return errorCode;
}

const char *Error::what() const noexcept {
    switch (errorCode) {
    case VRTD_RET_BAD_LIB_CALL:
        return "Bad library call";

    case VRTD_RET_BAD_CONN:
        return "Bad connection to daemon";

    case VRTD_RET_BAD_REQUEST:
        return "Bad request";

    case VRTD_RET_INVALID_ARGUMENT:
        return "Invalid argument";

    case VRTD_RET_NOEXIST:
        return "Requested resouce doesn't exist";

    case VRTD_RET_INTERNAL_ERROR:
        return "Internal error in vrtd daemon";

    case VRTD_RET_AUTH_ERROR:
        return "Missing permission";

    default:
        return "Unknown error";
    }
}

}
