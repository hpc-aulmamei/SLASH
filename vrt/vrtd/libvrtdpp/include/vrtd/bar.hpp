#ifndef VRTD_BAR_HPP
#define VRTD_BAR_HPP

#include <string>
#include <string_view>
#include <functional>
#include <stdint.h>

#include <vrtd/bar_file.hpp>

namespace vrtd {

class Bar {
public:
    uint32_t getDeviceNum() const noexcept;
    uint8_t getNum() const noexcept;
    bool isUsable() const noexcept;
    bool isInUse() const noexcept;
    uint64_t getStartAddress() const noexcept;
    uint64_t getLength() const noexcept;

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