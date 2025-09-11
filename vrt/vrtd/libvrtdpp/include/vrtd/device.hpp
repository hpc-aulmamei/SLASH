#ifndef VRTD_DEVICE_HPP
#define VRTD_DEVICE_HPP

#include <string>
#include <string_view>
#include <functional>
#include <stddef.h>
#include <stdint.h>

#include <vrtd/bar.hpp>

namespace vrtd {

class Device {
public:
    uint32_t getNum() const noexcept;
    const std::string& getName() const noexcept;

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
