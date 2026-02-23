/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "api/kernel.hpp"

#include "api/device.hpp"

namespace vrt {

Kernel::Kernel(const std::string& name, uint64_t baseAddr, uint64_t range,
               const std::vector<Register>& registers) {
    this->name = name;
    this->baseAddr = baseAddr;
    this->range = range;
    this->registers = registers;
}

Kernel::Kernel(Device& device, const std::string& kernelName)
    : Kernel(device.getKernel(kernelName)) {
    deviceBdf = device.getBdf();
    this->platform = device.getPlatform();
    this->server = device.getZmqServer();
    if (this->platform == Platform::HARDWARE) {
        const auto& vrtdDevice = device.getVrtdDevice();
        this->vrtdBar = vrtdDevice.getBar(bar);
    }
}

void Kernel::write(uint32_t offset, uint32_t value) {
    if (platform == Platform::HARDWARE) {
        utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__,
                           "Writing to device {} kernel: {} at offset: {x} value: {x}", deviceBdf,
                           name, offset, value);
        if (!vrtdBar.has_value()) {
            throw std::runtime_error("vrtd BAR handle not initialized");
        }

        auto barFile = vrtdBar->openBarFile();
        uint64_t barStart = vrtdBar->getStartAddress();
        if (baseAddr < barStart) {
            throw std::runtime_error("Kernel base address below BAR start");
        }
        uint64_t barOffset = (baseAddr - barStart) + offset;
        if (barOffset + sizeof(uint32_t) > barFile.getLen()) {
            throw std::runtime_error("BAR write out of range");
        }
        auto ptr = barFile.getPtr<uint32_t>(vrtd::BarFile::Direction::Write,
                                            static_cast<size_t>(barOffset));
        *ptr = value;
        return;
    } else if (platform == Platform::SIMULATION) {
        server->sendScalar(baseAddr + offset, value);
    }
}

uint32_t Kernel::read(uint32_t offset) {
    if (platform == Platform::HARDWARE) {
        if (offset != 0)
            utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__,
                               "Reading from device {} kernel: {} at offset: {x}", deviceBdf, name,
                               offset);
        if (!vrtdBar.has_value()) {
            throw std::runtime_error("vrtd BAR handle not initialized");
        }

        auto barFile = vrtdBar->openBarFile();
        uint64_t barStart = vrtdBar->getStartAddress();
        if (baseAddr < barStart) {
            throw std::runtime_error("Kernel base address below BAR start");
        }
        uint64_t barOffset = (baseAddr - barStart) + offset;
        if (barOffset + sizeof(uint32_t) > barFile.getLen()) {
            throw std::runtime_error("BAR read out of range");
        }
        auto ptr = barFile.getPtr<uint32_t>(vrtd::BarFile::Direction::Read,
                                            static_cast<size_t>(barOffset));
        return *ptr;
    } else if (platform == Platform::EMULATION) {
        currentRegisterIndex = 4;
        std::size_t argIdx = 0;
        while (currentRegisterIndex < registers.size()) {
            std::regex re(".*_\\d+$");
            if (std::regex_match(registers.at(currentRegisterIndex).getRegisterName(), re)) {
                currentRegisterIndex += 2;
            } else {
                if (registers.at(currentRegisterIndex).getOffset() == offset) {
                    return server->fetchScalar(name, "arg" + std::to_string(argIdx));
                }
                currentRegisterIndex++;
            }
            argIdx++;
        }
    } else if (platform == Platform::SIMULATION) {
        return server->fetchScalarSim(baseAddr + offset);
    }
    return 0;
}

void Kernel::setVrtdBar(const std::optional<vrtd::Bar>& bar) { this->vrtdBar = bar; }

void Kernel::wait() {
    if (platform == Platform::EMULATION) {
        return;
    }
    // ap_ctrl_hs: wait for ap_done (CTRL[1]) instead of checking exact control word values.
    while ((read(0x00) & 0x2u) == 0u) {
    }
}

void Kernel::startKernel(bool autorestart) {
    if (autorestart) {
        write(0x00, 0x81);
    } else {
        write(0x00, 0x01);
    }
}

Kernel::~Kernel() {}

void Kernel::setPlatform(Platform platform) { this->platform = platform; }

void Kernel::writeBatch() {
    if (platform != Platform::HARDWARE) {
        return;
    }
    uint32_t noOfPhysicalRegisters =
        (registers.at(registers.size() - 1).getOffset() + sizeof(uint32_t)) / sizeof(uint32_t);
    uint32_t* buf = (uint32_t*)calloc(noOfPhysicalRegisters, sizeof(uint32_t));
    for (std::size_t i = 4; i < noOfPhysicalRegisters; i++) {
        buf[i] = registerMap[i * sizeof(uint32_t)];
        // buf[i] = registerMap[registers.at(i).getOffset()];
        utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__,
                           "Kernel {}, reg at offset {x}, value: {x}", name, i * sizeof(uint32_t),
                           buf[i]);
    }
    if (!vrtdBar.has_value()) {
        free(buf);
        throw std::runtime_error("vrtd BAR handle not initialized");
    }

    auto barFile = vrtdBar->openBarFile();
    uint64_t barStart = vrtdBar->getStartAddress();
    if (baseAddr < barStart) {
        free(buf);
        throw std::runtime_error("Kernel base address below BAR start");
    }
    uint64_t barOffset = baseAddr - barStart;
    uint64_t byteCount = static_cast<uint64_t>(noOfPhysicalRegisters) * sizeof(uint32_t);
    if (barOffset + byteCount > barFile.getLen()) {
        free(buf);
        throw std::runtime_error("BAR write range out of range");
    }
    auto ptr = barFile.getPtr<uint32_t>(vrtd::BarFile::Direction::Write,
                                        static_cast<size_t>(barOffset));
    for (uint32_t i = 0; i < noOfPhysicalRegisters; ++i) {
        ptr[i] = buf[i];
    }
    free(buf);
    return;
}
std::string Kernel::getName() const { return name; }

}  // namespace vrt
