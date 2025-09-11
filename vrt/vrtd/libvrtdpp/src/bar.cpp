#include <vrtd/bar.hpp>

namespace vrtd {

Bar::Bar(uint32_t deviceNum, uint8_t num, bool usable, bool inUse, uint64_t startAddress, uint64_t length, std::function<BarFile(const Bar&)> fOpenBarFile) noexcept {
    this->deviceNum = deviceNum;
    this->num = num;
    this->usable = usable;
    this->inUse = inUse;
    this->startAddress = startAddress;
    this->length = length;
    this->fOpenBarFile = fOpenBarFile;
}

uint32_t Bar::getDeviceNum() const noexcept {
    return deviceNum;
}

uint8_t Bar::getNum() const noexcept {
    return num;
}

bool Bar::isUsable() const noexcept {
    return usable;
}

bool Bar::isInUse() const noexcept {
    return inUse;
}

uint64_t Bar::getStartAddress() const noexcept {
    return startAddress;
}

uint64_t Bar::getLength() const noexcept {
    return length;
}

BarFile Bar::openBarFile() const {
    return fOpenBarFile(*this);
}

}
