#include <vrtd/device.hpp>

namespace vrtd {

Device::Device(uint32_t num, std::string_view name, std::function<Bar(const Device&, uint8_t)> fGetBar) {
    this->num = num;
    this->name = name;
    this->fGetBar = fGetBar;
}

uint32_t Device::getNum() const noexcept {
    return num;
}

const std::string& Device::getName() const noexcept {
    return name;
}

Bar Device::getBar(uint8_t num) const {
    return fGetBar(*this, num);
}

}
