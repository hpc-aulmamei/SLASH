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

#include "utils/filesystem_cache.hpp"

#include <limits>
#include <ami.h>
#include <ami_sensor.h>
#include <vrtd/bar.hpp>

namespace vrt {

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

}  // namespace

Device::Device(const std::string& bdf, const std::string& vrtbinPath, bool program,
               ProgramType programType)
    : vrtbin(vrtbinPath, bdf) {
    this->bdf = normalizeBdfLegacy(bdf);
    this->bdfFull = normalizeBdfForVrtd(bdf);
    lockPcieDevice(this->bdf);
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
        findVrtbinType();
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
        std::string emulationExecPath = this->vrtbin.getEmulationExec() + " >/dev/null";

        std::thread([emulationExecPath]() { std::system(emulationExecPath.c_str()); }).detach();

    } else {
        parseSystemMap();
        std::string simulationExecPath = this->vrtbin.getSimulationExec() + " >/dev/null";

        std::thread([simulationExecPath]() { std::system(simulationExecPath.c_str()); }).detach();
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

Device::~Device() {
    unlockPcieDevice(bdf);
}

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
        if (dev != nullptr) {
            ami_dev_delete(&dev);
        }
        unlockPcieDevice(bdf);
    } else if (platform == Platform::EMULATION || platform == Platform::SIMULATION) {
        Json::Value exit;
        exit["command"] = "exit";
        zmqServer->sendCommand(exit);
    }
}

std::string Device::getBdf() { return bdf; }

void Device::programDevice() {
    auto ensurePdiList = [&]() {
        if (pdiPaths.empty() && !pdiPath.empty()) {
            pdiPaths.push_back(pdiPath);
        }
        if (pdiPaths.empty()) {
            throw std::runtime_error("No PDI files found for programming");
        }
    };

    if (programType == ProgramType::JTAG) {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Programming device {} in JTAG mode...This might take a while", bdf);
        ensurePdiList();
        for (const auto& pdi : pdiPaths) {
            std::string cmd = JTAG_PROGRAM_PATH + pdi;
            utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                               "Programming PDI via JTAG {}", pdi);
            system(cmd.c_str());
        }
        bootDevice();
        return;
    }

    if (vrtbinType == VrtbinType::SEGMENTED) {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Programming device {} in SEGMENTED mode...This might take a while",
                           bdf);
    } else {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Programming device {} in FLASH mode...This might take a while", bdf);
    }

    ensurePdiList();
    for (const auto& pdi : pdiPaths) {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Programming PDI via vrtd design writer {}", pdi);
        getVrtdDevice().designWriteFile(pdi);
    }

    bootDevice();
}

void Device::bootDevice() {
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Booting device...");
    auto writePmcGpio = [&]() {
        constexpr size_t kPmcGpioOffset = 0x1040000;
        constexpr uint32_t kPmcGpioValue = 1;

        auto barHandle = getVrtdDevice().getBar(0);
        auto barFile = barHandle.openBarFile();
        if (kPmcGpioOffset + sizeof(uint32_t) > barFile.getLen()) {
            throw std::runtime_error("PMC GPIO BAR write out of range");
        }
        auto ptr = barFile.getPtr<uint32_t>(vrtd::BarFile::Direction::Write, kPmcGpioOffset);
        *ptr = kPmcGpioValue;
    };

    if (vrtbinType == VrtbinType::FLAT) {
        if (programType == ProgramType::FLASH) {
            utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Booting into PDI...");
            utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__, "Writing PMC GPIO...");
            writePmcGpio();
            getVrtdDevice().hotplugRemove();
            getVrtdDevice().hotplugToggleSbr();
            getVrtdDevice().hotplugRescan();
            getVrtdDevice().hotplug();
        } else {
            utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Booting into PDI...");
            getVrtdDevice().hotplugRemove();
            getVrtdDevice().hotplugRescan();
            getVrtdDevice().hotplug();
        }
    } else if (vrtbinType == VrtbinType::SEGMENTED) {
        utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__,
                           "Booting segmented image into PDI...");
        utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__, "Writing PMC GPIO...");
        writePmcGpio();
        getVrtdDevice().hotplugRemove();
        getVrtdDevice().hotplugToggleSbr();
        getVrtdDevice().hotplugRescan();
        getVrtdDevice().hotplug();
        getVrtdDevice().hotplugRemove();
        usleep(2 * DELAY_PARTIAL_BOOT);  // enough time for the device to reset
        getVrtdDevice().hotplugRescan();
        getVrtdDevice().hotplug();
    }

    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "New PDI booted successfully");
    XMLParser parser(systemMap);
    parser.parseXML();
}

void Device::getNewHandle() {
    ami_device* new_dev = NULL;
    int ret = AMI_STATUS_ERROR;
    ret = ami_dev_find_next(&new_dev, AMI_PCI_BUS(pci_bdf), AMI_PCI_DEV(pci_bdf),
                            AMI_PCI_FUNC(pci_bdf), NULL);
    if (ret == AMI_STATUS_OK) {
        if (ami_sensor_discover(new_dev) == AMI_STATUS_OK) {
            dev = new_dev;
        } else {
            throw std::runtime_error("Failed to discover sensors");
        }
    } else {
        throw std::runtime_error("Failed to find device");
    }
}

void Device::createAmiDev() {
    if (ami_dev_find(bdf.c_str(), &dev) != AMI_STATUS_OK) {
        throw std::runtime_error("Failed to find device " + bdf);
    }
    ami_dev_get_pci_bdf(dev, &pci_bdf);
    if (ami_dev_request_access(dev) != AMI_STATUS_OK) {
        throw std::runtime_error("Failed to request elevated access to device");
    }
}

void Device::destroyAmiDev() {
    if (dev != nullptr) {
        ami_dev_delete(&dev);
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

void Device::findVrtbinType() {
    XMLParser parser(systemMap);
    parser.parseXML();
    this->vrtbinType = parser.getVrtbinType();
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

void Device::lockPcieDevice(const std::string& bdf) {
    std::string lockFile = FilesystemCache::getRuntimePath() / ("pcie_device_" + bdf + ".lock");
    int fd = open(lockFile.c_str(), O_CREAT | O_WRONLY, 0666);
    if (fd == -1) {
        throw std::runtime_error("Failed to lock PCIe device " + bdf);
    }
    int ret = flock(fd, LOCK_EX | LOCK_NB);
    if (ret < 0) {
        close(fd);
        throw std::runtime_error("Device " + bdf + " locked by another instance");
    }
}

void Device::unlockPcieDevice(const std::string& bdf) {
    std::string lockFile = FilesystemCache::getRuntimePath() / ("pcie_device_" + bdf + ".lock");
    int fd = open(lockFile.c_str(), O_WRONLY, 0666);
    if (fd == -1) {
        throw std::runtime_error("Failed to lock PCIe device " + bdf);
    }
    int ret = flock(fd, LOCK_UN);
    if (ret < 0) {
        throw std::runtime_error("Device " + bdf + " cannot be unlocked");
    }
    close(fd);
}

}  // namespace vrt
