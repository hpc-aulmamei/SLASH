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

#ifndef KERNEL_HPP
#define KERNEL_HPP

#include <json/json.h>

#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "register/register.hpp"
#include "utils/logger.hpp"
#include "utils/platform.hpp"
#include "utils/zmq_server.hpp"
#include <vrtd/bar.hpp>

namespace vrt {
class Device;
template <typename T>
class Buffer;

/**
 * @brief Class representing a kernel.
 */
class Kernel {
    uint8_t bar = 0;                                          ///< Base Address Register (BAR)
    std::string name;                                         ///< Name of the kernel
    uint64_t baseAddr;                                        ///< Base address of the kernel
    uint64_t range;                                           ///< Address range of the kernel
    std::vector<Register> registers;                          ///< List of registers in the kernel
    size_t currentRegisterIndex = 4;           ///< Index of the current register being processed
    std::string deviceBdf;                     ///< BDF of the device
    Platform platform;                         ///< Platform of the device
    std::shared_ptr<ZmqServer> server;         ///< Pointer to ZeroMQ server for communication
    std::map<uint32_t, uint32_t> registerMap;  ///< Map of register offsets to values
    std::optional<vrtd::Bar> vrtdBar;          ///< vrtd BAR handle for hardware access
    std::vector<std::string> emuCallArgKinds;  ///< Optional EMU arg kind metadata from emu_manifest.json
    std::map<uint32_t, std::string> emuFetchScalarArgByOffset;  ///< Optional EMU fetch routing by register offset

    template <typename T, typename = void>
    struct HasPhysAddr : std::false_type {};

    template <typename T>
    struct HasPhysAddr<T, std::void_t<decltype(std::declval<const T&>().getPhysAddr())>>
        : std::true_type {};

    template <typename T>
    static uint64_t resolveKernelArgImpl(T&& arg, std::true_type) {
        return static_cast<uint64_t>(arg.getPhysAddr());
    }

    template <typename T>
    static decltype(auto) resolveKernelArgImpl(T&& arg, std::false_type) {
        return std::forward<T>(arg);
    }

    template <typename T>
    static decltype(auto) resolveKernelArg(T&& arg) {
        using ArgT = std::remove_reference_t<T>;
        return resolveKernelArgImpl(std::forward<T>(arg), HasPhysAddr<ArgT>{});
    }
   public:
    /**
     * @brief Constructor for Kernel.
     * @param name The name of the kernel.
     * @param baseAddr The base address of the kernel.
     * @param range The address range of the kernel.
     * @param registers The list of registers in the kernel.
     */
    Kernel(const std::string& name, uint64_t baseAddr, uint64_t range,
           const std::vector<Register>& registers);

    /**
     * @brief Default constructor for Kernel.
     */
    Kernel() = default;

    /**
     * @brief Default copy constructor for Kernel.
     */
    Kernel(const Kernel&) = default;

    /**
     * @brief Default move constructor for Kernel.
     */
    Kernel(Kernel&&) = default;

    /**
     * @brief Constructor for Kernel using a Device object.
     * @param device The Device object.
     * @param kernelName The name of the kernel.
     */
    Kernel(vrt::Device device, const std::string& kernelName);

    /**
     * @brief Sets the vrtd BAR handle for hardware access.
     * @param bar The vrtd BAR handle.
     */
    void setVrtdBar(const std::optional<vrtd::Bar>& bar);

    /**
     * @brief Writes a value to a register.
     * @param offset The offset of the register.
     * @param value The value to write.
     */
    void write(uint32_t offset, uint32_t value);

    /**
     * @brief Reads a value from a register.
     * @param offset The offset of the register.
     * @return The value read from the register.
     */
    uint32_t read(uint32_t offset);

    /**
     * @brief Waits for the kernel to complete.
     */
    void wait();

    /**
     * @brief Starts the kernel.
     * @param autorestart Flag indicating whether to enable autorestart.
     */
    void startKernel(bool autorestart = false);

    /**
     * @brief Sets the platform for the kernel.
     * @param platform The platform to set.
     */
    void setPlatform(Platform platform);

    /**
     * @brief Sets EMU call argument kinds loaded from emu_manifest.json.
     *        Index corresponds to argN in EMU call JSON.
     */
    void setEmuCallArgKinds(const std::vector<std::string>& kinds);

    /**
     * @brief Sets EMU scalar fetch routing keyed by register offset.
     *        Used by Kernel::read() in EMULATION mode.
     */
    void setEmuFetchScalarArgByOffset(const std::map<uint32_t, std::string>& routes);

    /**
     * @brief Writes batch register to PCIe BAR.
     */
    void writeBatch();

    /**
     * @brief Calls the kernel and waits for it to complete.
     * @param args The arguments to pass to the kernel.
     */
    template <typename... Args>
    void call(Args&&... args) {
        currentRegisterIndex = 4;
        if (platform == Platform::HARDWARE) {
            (processArg(std::forward<Args>(args)), ...);
            this->writeBatch();
            this->startKernel();
            this->wait();
        } else if (platform == Platform::EMULATION) {
            Json::Value command;
            command["command"] = "call";
            command["function"] = name;
            int argIdx = 0;
            (processEmuArg(std::forward<Args>(args), command, argIdx), ...);
            server->sendCommand(command);
        } else if (platform == Platform::SIMULATION) {
            (processSimArg(std::forward<Args>(args)), ...);
            this->startKernel();
            this->wait();
        }
    }

    /**
     * @brief Starts the kernel.
     * @param args The arguments to pass to the kernel.
     */
    template <typename... Args>
    void start(Args&&... args) {
        currentRegisterIndex = 4;
        if (platform == Platform::HARDWARE) {
            (processArg(std::forward<Args>(args)), ...);
            this->writeBatch();
            this->startKernel();

        } else if (platform == Platform::EMULATION) {
            Json::Value command;
            command["command"] = "start";
            command["function"] = name;
            int argIdx = 0;
            (processEmuArg(std::forward<Args>(args), command, argIdx), ...);
            server->sendCommand(command);
        } else if (platform == Platform::SIMULATION) {
            (processSimArg(std::forward<Args>(args)), ...);
            this->startKernel();
        }
    }
    /**
     * @brief Helper method which processes an argument.
     * @tparam T The type of the argument.
     * @param arg The argument to process.
     */
    template <typename T>
    void processArg(T&& arg) {
        decltype(auto) resolvedArg = resolveKernelArg(std::forward<T>(arg));
        if (currentRegisterIndex < registers.size()) {
            std::regex re(".*_\\d+$");  // Regular expression to match strings ending with _nr
            if (std::regex_match(registers.at(currentRegisterIndex).getRegisterName(), re)) {
                this->registerMap[registers.at(currentRegisterIndex).getOffset()] =
                    static_cast<uint32_t>(static_cast<uint64_t>(resolvedArg) & 0xFFFFFFFFULL);
                this->registerMap[registers.at(currentRegisterIndex + 1).getOffset()] =
                    static_cast<uint32_t>((static_cast<uint64_t>(resolvedArg) >> 32) &
                                          0xFFFFFFFFULL);
                currentRegisterIndex += 2;
            } else {
                this->registerMap[registers.at(currentRegisterIndex).getOffset()] =
                    static_cast<uint32_t>(resolvedArg);
                currentRegisterIndex++;
            }

        } else {
            throw std::runtime_error("Not enough registers to process all arguments.");
        }
    }

    /**
     * @brief Helper method which processes an argument for simulation.
     * @tparam T The type of the argument.
     * @param arg The argument to process.
     */
    template <typename T>
    void processSimArg(T&& arg) {
        decltype(auto) resolvedArg = resolveKernelArg(std::forward<T>(arg));
        if (currentRegisterIndex < registers.size()) {
            std::regex re(".*_\\d+$");  // Regular expression to match strings ending with _nr
            if (std::regex_match(registers.at(currentRegisterIndex).getRegisterName(), re)) {
                this->write(registers.at(currentRegisterIndex).getOffset(),
                            static_cast<uint32_t>(static_cast<uint64_t>(resolvedArg) &
                                                  0xFFFFFFFFULL));
                this->write(registers.at(currentRegisterIndex + 1).getOffset(),
                            static_cast<uint32_t>((static_cast<uint64_t>(resolvedArg) >> 32) &
                                                  0xFFFFFFFFULL));
                currentRegisterIndex += 2;
            } else {
                this->write(registers.at(currentRegisterIndex).getOffset(),
                            static_cast<uint32_t>(resolvedArg));
                currentRegisterIndex++;
            }
        }
    }

    /**
     * @brief Helper method which processes an argument for emulation.
     * @tparam T The type of the argument.
     * @param arg The argument to process.
     * @param command The JSON command to update.
     * @param argIndex The index of the argument.
     */
    template <typename T>
    void processEmuArg(T&& arg, Json::Value& command, int& argIndex) {
        decltype(auto) resolvedArg = resolveKernelArg(std::forward<T>(arg));
        if (currentRegisterIndex < registers.size()) {
            std::regex re(".*_\\d+$");  // Regular expression to match strings ending with _nr
            const bool isSplitReg =
                std::regex_match(registers.at(currentRegisterIndex).getRegisterName(), re);

            std::string emuKind;
            if (platform == Platform::EMULATION &&
                static_cast<std::size_t>(argIndex) < emuCallArgKinds.size()) {
                emuKind = emuCallArgKinds[static_cast<std::size_t>(argIndex)];
            }

            if (emuKind.empty()) {
                throw std::runtime_error("EMU manifest arg kind missing for kernel '" + name +
                                         "' at arg" + std::to_string(argIndex));
            }

            if (emuKind == "buffer") {
                command["args"]["arg" + std::to_string(argIndex)]["type"] = "buffer";
                command["args"]["arg" + std::to_string(argIndex)]["name"] =
                    std::to_string(static_cast<uint64_t>(resolvedArg));
            } else if (emuKind == "scalar") {
                command["args"]["arg" + std::to_string(argIndex)]["type"] = "scalar";
                command["args"]["arg" + std::to_string(argIndex)]["value"] = resolvedArg;
            } else {
                throw std::runtime_error("Unsupported EMU manifest arg kind '" + emuKind +
                                         "' for kernel '" + name + "' at arg" +
                                         std::to_string(argIndex));
            }

            currentRegisterIndex += isSplitReg ? 2 : 1;
            argIndex++;
        } else {
            throw std::runtime_error("Not enough registers to process all arguments.");
        }
    }

    /**
     * @brief Getter for the kernel name.
     * @return The name of the kernel.
     */
    std::string getName() const;

    /**
     * @brief Getter for the kernel base physical address.
     * @return The physical base address of the kernel.
     */
    uint64_t getPhysAddr() const;

    /**
     * @brief Destructor for Kernel.
     */
    ~Kernel();

    /**
     * @brief Copy assignment operator.
     *
     * @param other The kernel to copy from.
     * @return Reference to this kernel.
     */
    Kernel& operator=(const Kernel& other) = default;

    /**
     * @brief Move assignment operator.
     *
     * @param other The kernel to move from.
     * @return Reference to this kernel.
     */
    Kernel& operator=(Kernel&& other) noexcept = default;
};

}  // namespace vrt

#endif  // KERNEL_HPP
