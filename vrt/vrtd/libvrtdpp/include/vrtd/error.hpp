#ifndef VRTD_ERROR_H
#define VRTD_ERROR_H

#include <stdexcept>

#include <vrtd/vrtd.h>

namespace vrtd {

class Error : public std::exception {
private:
    vrtd_ret errorCode;

public:
    explicit Error(vrtd_ret errorCode) noexcept;

    vrtd_ret getErrorCode() const noexcept;
    const char *what() const noexcept override;
};

}

#endif //VRTD_ERROR_H
