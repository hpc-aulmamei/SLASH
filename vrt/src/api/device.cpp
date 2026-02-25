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

#include "api/device.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vrtd/bar.hpp>

#include "utils/filesystem_cache.hpp"

namespace vrt {
namespace impl {

namespace {

std::string normalizeBdfForVrtd(const std::string& bdf) {
    const auto firstColon = bdf.find(':');
    const auto lastColon = bdf.rfind(':');
    if (firstColon != std::string::npos && firstColon != lastColon) {
        return bdf;
    }
    return "0000:" + bdf;
}

std::string normalizeBdfLegacy(const std::string& bdf) {
    const auto firstColon = bdf.find(':');
    const auto lastColon = bdf.rfind(':');
    if (firstColon != std::string::npos && firstColon != lastColon) {
        return bdf.substr(firstColon + 1);
    }
    return bdf;
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string makeExecFromBinaryDirCommand(const std::string& execPath) {
    const std::filesystem::path path(execPath);
    const std::string dir = path.parent_path().string();
    const std::string file = path.filename().string();
    if (dir.empty() || file.empty()) {
        return shellQuote(execPath);
    }
    return "cd " + shellQuote(dir) + " && exec ./" + shellQuote(file);
}

}  // namespace

Device::Device(const std::string& bdf, const std::string& vrtbinPath, bool program,
               ProgramType programType)
    : vrtbin(vrtbinPath, bdf) {
    this->bdf = normalizeBdfLegacy(bdf);
    this->bdfFull = normalizeBdfForVrtd(bdf);
    this->allocator = new Allocator();
    this->systemMap = this->vrtbin.getSystemMapPath();
    this->pdiPath = this->vrtbin.getPdiPath();
    this->pdiPaths = this->vrtbin.getPdiPaths();
    this->programType = programType;
    this->zmqServer = std::make_shared<ZmqServer>();
    findPlatform();
    if (platform == Platform::HARDWARE) {
        vrtdSession = std::make_shared<vrtd::Session>();
        vrtdDevice = vrtdSession->getDeviceByBdf(bdfFull);
        if (program) {
            programDevice();
        }
        parseSystemMap();
        if (vrtdDevice.has_value()) {
            if (clockFreq > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("Clock frequency from system map exceeds vrtd clock API limits");
            }
            vrtdDevice->setUserClockRate(static_cast<uint32_t>(clockFreq));
        }
    } else if (platform == Platform::EMULATION) {
        parseSystemMap();
        std::string emulationExecPath = this->vrtbin.getEmulationExec();
        if (emulationExecPath.empty()) {
            throw std::runtime_error("Emulation executable vpp_emu not found in vrtbin");
        }
        if (::access(emulationExecPath.c_str(), X_OK) != 0) {
            throw std::runtime_error("Emulation executable is not runnable: " + emulationExecPath +
                                     " (" + std::strerror(errno) + ")");
        }

        const std::string emuCommand = makeExecFromBinaryDirCommand(emulationExecPath);
        std::thread([emuCommand]() { std::system(emuCommand.c_str()); }).detach();

    } else {
        parseSystemMap();
        std::string simulationExecPath = this->vrtbin.getSimulationExec();
        if (simulationExecPath.empty()) {
            throw std::runtime_error("Simulation executable vpp_sim not found in vrtbin");
        }
        if (::access(simulationExecPath.c_str(), X_OK) != 0) {
            throw std::runtime_error("Simulation executable is not runnable: " +
                                     simulationExecPath + " (" + std::strerror(errno) + ")");
        }

        const std::string simCommand = makeExecFromBinaryDirCommand(simulationExecPath);
        std::thread([simCommand]() { std::system(simCommand.c_str()); }).detach();
        Json::Value command;
        command["command"] = "start";
        zmqServer->sendCommand(command);
    }
    if (platform == Platform::HARDWARE && vrtdDevice.has_value()) {
        for (auto& qdmaCon : qdmaConnections) {
            qdmaIntfs.emplace_back(new QdmaIntf(*vrtdDevice, qdmaCon.getQid(),
                                                qdmaCon.getDirection()));
        }
    }
}

Device::~Device() = default;

void Device::parseSystemMap() {
    XMLParser parser(systemMap);
    parser.parseXML();
    clockFreq = parser.getClockFrequency();
    this->platform = parser.getPlatform();
    kernels = parser.getKernels();
    if (platform == Platform::HARDWARE && vrtdDevice.has_value()) {
        std::optional<vrtd::Bar> barHandle = vrtdDevice->getBar(bar);
        for (auto& kernel : kernels) {
            kernel.second.setVrtdBar(barHandle);
        }
    }
    this->qdmaConnections = parser.getQdmaConnections();
}

Kernel Device::getKernel(const std::string& name) { return kernels[name]; }

void Device::cleanup() {
    if (platform == Platform::HARDWARE) {
        for (auto qdmaIntf_ : qdmaIntfs) {
            delete qdmaIntf_;
        }
    } else if (platform == Platform::EMULATION || platform == Platform::SIMULATION) {
        Json::Value exit;
        exit["command"] = "exit";
        zmqServer->sendCommand(exit);
    }
}

std::string Device::getBdf() { return bdf; }

void Device::programDevice() {
    if (pdiPaths.empty() && !pdiPath.empty()) {
        pdiPaths.push_back(pdiPath);
    }
    if (pdiPaths.empty()) {
        throw std::runtime_error("No PDI files found for programming");
    }

    for (const auto& pdi : pdiPaths) {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Programming PDI via vrtd design writer {}", pdi);
        getVrtdDevice().designWriteFile(pdi);
    }
}

void Device::setFrequency(uint64_t freq) {
    if (platform == Platform::HARDWARE) {
        if (freq > clockFreq) {
            utils::Logger::log(utils::LogLevel::WARN, __PRETTY_FUNCTION__,
                               "Setting frequency {}, which is higher than max frequency {}", freq,
                               clockFreq);
        }
        if (freq > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Requested frequency exceeds vrtd clock API limits");
        }
        getVrtdDevice().setUserClockRate(static_cast<uint32_t>(freq));
    }
}

uint64_t Device::getFrequency() {
    if (platform == Platform::HARDWARE) {
        return getVrtdDevice().getUserClockRate();
    } else {
        return 0;
    }
}

uint64_t Device::getMaxFrequency() {
    if (platform == Platform::HARDWARE) {
        return clockFreq;
    } else {
        return 0;
    }
}

void Device::findPlatform() {
    XMLParser parser(systemMap);
    parser.parseXML();
    this->platform = parser.getPlatform();
}

Platform Device::getPlatform() { return platform; }

std::shared_ptr<ZmqServer> Device::getZmqServer() { return zmqServer; }

std::vector<QdmaConnection> Device::getQdmaConnections() { return qdmaConnections; }

Allocator* Device::getAllocator() { return allocator; }

vrtd::Device& Device::getVrtdDevice() {
    if (!vrtdDevice.has_value()) {
        throw std::runtime_error("vrtd device not initialized");
    }
    return *vrtdDevice;
}

const vrtd::Device& Device::getVrtdDevice() const {
    if (!vrtdDevice.has_value()) {
        throw std::runtime_error("vrtd device not initialized");
    }
    return *vrtdDevice;
}

std::vector<QdmaIntf*> Device::getQdmaInterfaces() { return qdmaIntfs; }

}  // namespace impl
}  // namespace vrt
