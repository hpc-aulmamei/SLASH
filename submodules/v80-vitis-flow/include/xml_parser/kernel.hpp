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

#pragma once
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "area_estimates.hpp"
#include "interface.hpp"
#include "logger.hpp"
#include "register.hpp"

/**
 * @brief Enumeration for specifying resource type categories.
 */
enum ResourceType {
    RESOURCE_TYPE_USED = 0,  ///< Resources used by the kernel
    RESOURCE_TYPE_AVAILABLE  ///< Resources available on the target device
};

/**
 * @brief Class representing a hardware kernel in an FPGA design.
 *
 * This class stores information about a hardware kernel including its build properties,
 * clock settings, resource utilization, interfaces, and registers. It provides methods
 * to access and modify these properties for design analysis and implementation.
 */
class Kernel {
    std::string buildVersion;           ///< Version of the build tools
    std::string unit;                   ///< Unit of measurement for timing (typically ns)
    std::string productFamily;          ///< FPGA product family (e.g., "Virtex", "Versal")
    std::string part;                   ///< Specific FPGA part number
    std::string topModelName;           ///< Name of the top-level module
    std::string name;                   ///< Kernel name for identification
    float targetClk;                    ///< Target clock frequency in MHz
    float clkUncertainty;               ///< Clock uncertainty in ns
    float estimatedClk;                 ///< Estimated achievable clock frequency in MHz
    AreaEstimates estimates;            ///< Resource utilization estimates
    std::vector<Interface> interfaces;  ///< List of kernel interfaces
    std::vector<Register> registers;    ///< List of registers in the kernel
    bool networkKernel;               ///< Flag indicating if the kernel is a network kernel

   public:
    /**
     * @brief Default constructor for Kernel.
     */
    Kernel() = default;

    /**
     * @brief Gets the build version of the tools used.
     * @return String containing the build version.
     */
    std::string getBuildVersion();

    /**
     * @brief Sets the build version of the tools.
     * @param buildVersion String containing the build version.
     */
    void setBuildVersion(const std::string& buildVersion);

    /**
     * @brief Gets the time unit used for timing measurements.
     * @return String containing the time unit (typically "ns").
     */
    std::string getUnit();

    /**
     * @brief Sets the time unit for timing measurements.
     * @param unit String containing the time unit.
     */
    void setUnit(const std::string& unit);

    /**
     * @brief Gets the FPGA product family.
     * @return String containing the product family name.
     */
    std::string getProductFamily();

    /**
     * @brief Sets the FPGA product family.
     * @param family String containing the product family name.
     */
    void setProductFamily(const std::string& family);

    /**
     * @brief Gets the FPGA part number.
     * @return String containing the FPGA part number.
     */
    std::string getPart();

    /**
     * @brief Sets the FPGA part number.
     * @param part String containing the FPGA part number.
     */
    void setPart(const std::string& part);

    /**
     * @brief Gets the top module name of the kernel.
     * @return String containing the top module name.
     */
    std::string getTopModelName();

    /**
     * @brief Sets the top module name of the kernel.
     * @param modelName String containing the top module name.
     */
    void setTopModelName(const std::string& modelName);

    /**
     * @brief Gets the target clock frequency.
     * @return Target clock frequency in MHz.
     */
    float getTargetClk();

    /**
     * @brief Sets the target clock frequency.
     * @param clk Target clock frequency in MHz.
     */
    void setTargetClk(float clk);

    /**
     * @brief Gets the clock uncertainty.
     * @return Clock uncertainty in ns.
     */
    float getClkUncertainty();

    /**
     * @brief Sets the clock uncertainty.
     * @param uncertainty Clock uncertainty in ns.
     */
    void setClkUncertainty(float uncertainty);

    /**
     * @brief Gets the estimated achievable clock frequency.
     * @return Estimated achievable clock frequency in MHz.
     */
    float getEstimatedClk();

    /**
     * @brief Sets the estimated achievable clock frequency.
     * @param clk Estimated achievable clock frequency in MHz.
     */
    void setEstimatedClk(float clk);

    /**
     * @brief Gets the resource utilization estimates.
     * @return AreaEstimates object containing resource utilization.
     */
    AreaEstimates getAreaEstimates();

    /**
     * @brief Adds a resource estimate to the kernel.
     * @param type Type of resource (used or available).
     * @param resource Resource object to add.
     */
    void addEstimate(ResourceType type, Resource resource);

    /**
     * @brief Gets the list of interfaces for this kernel.
     * @return Vector of Interface objects for this kernel.
     */
    std::vector<Interface> getInterfaces();

    /**
     * @brief Adds an interface to the kernel.
     * @param interface Interface object to add.
     */
    void addInterface(Interface interface);

    /**
     * @brief Gets the list of registers for this kernel.
     * @return Vector of Register objects for this kernel.
     */
    std::vector<Register> getRegisters();

    /**
     * @brief Adds a register to the kernel.
     * @param reg Register object to add.
     */
    void addRegister(Register reg);

    /**
     * @brief Sets the name of the kernel.
     * @param name String containing the kernel name.
     */
    void setName(const std::string& name);

    /**
     * @brief Gets the name of the kernel.
     * @return String containing the kernel name.
     */
    std::string getName();

    /**
     * @brief Prints detailed information about the kernel.
     *
     * This method outputs comprehensive information about the kernel including
     * its build properties, clock settings, resource utilization, interfaces,
     * and registers for debugging or documentation.
     */
    void print();

    /**
     * @brief Checks if the kernel is a network kernel.
     * @return True if the kernel is a network kernel, false otherwise.
     */
    void setNetworkKernel();

    /**
     * @brief Checks if the kernel is a network kernel.
     * @return True if the kernel is a network kernel, false otherwise.
     */
    bool isNetworkKernel();

};