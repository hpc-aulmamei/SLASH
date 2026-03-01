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
namespace {

uint64_t resolveBarOffset(uint64_t absoluteAddr, uint64_t accessSize, uint64_t barLen) {
    if (barLen == 0) {
        throw std::runtime_error("BAR length is zero");
    }

    // Design model: BAR maps a contiguous AXI window. Kernel base addresses
    // are absolute within that window; register offsets are relative to kernel base.
    const uint64_t barWindowBase = absoluteAddr - (absoluteAddr % barLen);
    const uint64_t barOffset = absoluteAddr - barWindowBase;
    if (barOffset + accessSize > barLen) {
        throw std::runtime_error("BAR access out of range");
    }
    return barOffset;
}

}  // namespace

Kernel::Kernel(const std::string& name, uint64_t baseAddr, uint64_t range,
               const std::vector<Register>& registers) {
    this->name = name;
    this->baseAddr = baseAddr;
    this->range = range;
    this->registers = registers;
}

Kernel::Kernel(Device device, const std::string& kernelName)
    : Kernel(device.getKernel(kernelName)) {
    deviceBdf = device.getBdf();
    this->platform = device.getPlatform();
    this->server = device.getHandle()->getZmqServer();
    if (this->platform == Platform::HARDWARE) {
        const auto& vrtdDevice = device.getHandle()->getVrtdDevice();
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
        const uint64_t absoluteAddr = baseAddr + static_cast<uint64_t>(offset);
        uint64_t barOffset = resolveBarOffset(absoluteAddr, sizeof(uint32_t), barFile.getLen());
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
        const uint64_t absoluteAddr = baseAddr + static_cast<uint64_t>(offset);
        uint64_t barOffset = resolveBarOffset(absoluteAddr, sizeof(uint32_t), barFile.getLen());
        auto ptr = barFile.getPtr<uint32_t>(vrtd::BarFile::Direction::Read,
                                            static_cast<size_t>(barOffset));
        return *ptr;
    } else if (platform == Platform::EMULATION) {
        return server->readRegister(name, offset);
    } else if (platform == Platform::SIMULATION) {
        return server->fetchScalarSim(baseAddr + offset);
    }
    return 0;
}

void Kernel::setVrtdBar(const std::optional<vrtd::Bar>& bar) { this->vrtdBar = bar; }

void Kernel::setEmuCallArgKinds(const std::vector<std::string>& kinds) { emuCallArgKinds = kinds; }

void Kernel::setEmuFetchScalarArgByOffset(const std::map<uint32_t, std::string>& routes) {
    emuFetchScalarArgByOffset = routes;
}

void Kernel::wait() {
    if (platform == Platform::EMULATION) {
        Json::Value command;
        command["command"] = "wait";
        command["function"] = name;
        server->sendCommand(command);
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
    uint64_t byteCount = static_cast<uint64_t>(noOfPhysicalRegisters) * sizeof(uint32_t);
    uint64_t barOffset = 0;
    try {
        barOffset = resolveBarOffset(baseAddr, byteCount, barFile.getLen());
    } catch (const std::runtime_error&) {
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
uint64_t Kernel::getPhysAddr() const { return baseAddr; }

}  // namespace vrt
