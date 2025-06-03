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

#ifndef ARG_HPP
#define ARG_HPP

#include <string>
#include <cstdint>

/**
 * @brief Class representing a function argument.
 *
 * This class stores information about an argument passed to a function,
 * including its name, position index, and argument type.
 */
class Arg {
   private:
    std::string name;     ///< Name of the argument
    uint32_t index;       ///< Index position of the argument in the kernel signature
    std::string argType;  ///< Type of the argument (e.g., "scalar", "buffer")

   public:
    /**
     * @brief Constructor for Arg with initialization parameters.
     * @param name Name of the kernel argument.
     * @param index Index position of the argument in the kernel signature.
     * @param argType Type of the argument (e.g., "scalar", "buffer").
     */
    Arg(std::string name, uint32_t index, std::string argType);

    /**
     * @brief Gets the name of the argument.
     * @return String containing the argument name.
     */
    std::string getName();

    /**
     * @brief Gets the index position of the argument.
     * @return Index position as a 32-bit unsigned integer.
     */
    uint32_t getIndex();

    /**
     * @brief Gets the type of the argument.
     * @return String describing the argument type (e.g., "scalar", "buffer").
     */
    std::string getArgType();
};

#endif  // ARG_HPP
