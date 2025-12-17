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
#include "kernel.hpp"

std::string Kernel::getBuildVersion() { return this->buildVersion; }
void Kernel::setBuildVersion(const std::string& bv) { this->buildVersion = bv; }

std::string Kernel::getUnit() { return this->unit; }
void Kernel::setUnit(const std::string& unit) { this->unit = unit; }

std::string Kernel::getProductFamily() { return this->productFamily; }
void Kernel::setProductFamily(const std::string& pf) { this->productFamily = pf; }

std::string Kernel::getPart() { return this->part; }
void Kernel::setPart(const std::string& p) { this->part = p; }

std::string Kernel::getTopModelName() { return this->topModelName; }
void Kernel::setTopModelName(const std::string& tm) { this->topModelName = tm; }

float Kernel::getTargetClk() { return this->targetClk; }
void Kernel::setTargetClk(float tclk) { this->targetClk = tclk; }

float Kernel::getClkUncertainty() { return this->clkUncertainty; }
void Kernel::setClkUncertainty(float clkUncertainty) { this->clkUncertainty = clkUncertainty; }

float Kernel::getEstimatedClk() { return this->estimatedClk; }
void Kernel::setEstimatedClk(float estimatedClk) { this->estimatedClk = estimatedClk; }

AreaEstimates Kernel::getAreaEstimates() { return this->estimates; }
void Kernel::addEstimate(ResourceType type, Resource resource) {
    if (type == RESOURCE_TYPE_USED) {
        this->estimates.addResource(resource);
    } else {
        this->estimates.addAvailableResource(resource);
    }
}

std::vector<Interface> Kernel::getInterfaces() { return this->interfaces; }
void Kernel::addInterface(Interface intf) { this->interfaces.emplace_back(intf); }

void Kernel::addRegister(Register r) { this->registers.emplace_back(r); }

std::vector<Register> Kernel::getRegisters() { return this->registers; }

void Kernel::setName(const std::string& instName) { this->name = instName; }
std::string Kernel::getName() { return name; }

void Kernel::print() {
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Kernel instantiation name: {}",
                       name);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Build Version: {}",
                       buildVersion);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Time unit: {}", unit);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Product family: {}",
                       productFamily);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Product part: {}", part);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Top function name: {}",
                       topModelName);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Target clock period: {} {}",
                       targetClk, unit);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Target clock freq: {} MHz",
                       (1 / targetClk) * 1000);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Clock uncertainty: {}",
                       clkUncertainty);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Estimated clock: {} {}",
                       estimatedClk, unit);
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Estimated clock freq: {} MHz",
                       (1 / estimatedClk) * 1000);
    estimates.print();
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Interfaces:");
    for (auto el : interfaces) {
        el.print();
    }
    utils::Logger::log(utils::LogLevel::INFO, __PRETTY_FUNCTION__, "Registers:");
    for (auto& el : registers) {
        el.print();
    }
}

void Kernel::setNetworkKernel() { this->networkKernel = true; }

bool Kernel::isNetworkKernel() { return this->networkKernel; }