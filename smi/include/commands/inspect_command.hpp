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

#ifndef INSPECT_COMMAND_HPP
#define INSPECT_COMMAND_HPP

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <string>

#include "utils/vrtbin.hpp"

#define INSPECT_SYSTEM_MAP_PATH ((FilesystemCache::getCachePath() / "system_map.xml").c_str())  ///< Path to the system map XML file
#define INSPECT_VERSION_PATH ((FilesystemCache::getCachePath() / "version.json").c_str())       ///< Path to the version JSON file

/**
 * @brief Class for inspecting VRTBIN files.
 *
 * The InspectCommand class provides functionality to inspect and analyze VRTBIN
 * files, extracting metadata.
 */
class InspectCommand {
   public:
    /**
     * @brief Constructor for InspectCommand.
     *
     * @param image_path Path to the VRTBIN file to inspect.
     */
    InspectCommand(const std::string &image_path);

    /**
     * @brief Executes the inspection command.
     *
     * This method processes the VRTBIN file and displays relevant information.
     */
    void execute();

   private:
    std::string imagePath;  ///< Path to the VRTBIN file being inspected.

    /**
     * @brief Queries metadata from the VRTBIN file.
     *
     * Extracts and processes metadata information from the VRTBIN file.
     */
    void queryMetadata();
};

#endif  // INSPECT_COMMAND_HPP