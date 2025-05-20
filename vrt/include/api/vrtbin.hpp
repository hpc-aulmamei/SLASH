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

#ifndef VRTBIN_HPP
#define VRTBIN_HPP

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "parser/xml_parser.hpp"
#include "utils/logger.hpp"
#include "utils/platform.hpp"
#include "utils/filesystem_cache.hpp"

namespace vrt {

/**
 * @brief Class for handling VRTBIN operations.
 */
class Vrtbin {
    std::string vrtbinPath;                                         ///< Path to the VRTBIN file
    std::string systemMapPath;                                      ///< Path to the system map file
    std::string versionPath;                                        ///< Path to the version file
    std::string pdiPath;                                            ///< Path to the PDI file
    std::string uuid;                                               ///< UUID of the VRTBIN
    std::string tempExtractPath = FilesystemCache::getCachePath();  ///< Temporary extraction path
    std::string emulationExecPath;                                  ///< Path to the emulation executable
    std::string simulationExecPath;                                 ///< Path to the simulation executable
    Platform platform;                                              ///< Platform type
    /**
     * @brief Copies a file from source to destination.
     * @param source The source file path.
     * @param destination The destination file path.
     */
    void copy(const std::string& source, const std::string& destination);

   public:
    /**
     * @brief Constructor for Vrtbin.
     * @param vrtbinPath The path to the VRTBIN file.
     * @param bdf The Bus:Device.Function identifier.
     */
    Vrtbin(std::string vrtbinPath, const std::string& bdf);

    /**
     * @brief Extracts the VRTBIN file.
     */
    void extract();

    /**
     * @brief Gets the path to the system map file.
     * @return The path to the system map file.
     */
    std::string getSystemMapPath();

    /**
     * @brief Gets the path to the PDI file.
     * @return The path to the PDI file.
     */
    std::string getPdiPath();

    /**
     * @brief Gets the UUID of the VRTBIN.
     * @return The UUID of the VRTBIN.
     */
    std::string getUUID();

    /**
     * @brief Extracts the UUID from the VRTBIN file.
     */
    void extractUUID();

    /**
     * @brief Gets the emulation executable file.
     * @return The path to the emulation executable file.
     */
    std::string getEmulationExec();

    /**
     * @brief Gets the simulation executable file.
     * @return The path to the simulation executable file.
     */
    std::string getSimulationExec();
};

}  // namespace vrt

#endif  // VRTBIN_HPP