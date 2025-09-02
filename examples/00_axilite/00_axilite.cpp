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

#include <iostream>
#include <cstring> // for std::memcpy
#include <random>
#include <chrono>

#include <utils/logger.hpp>
#include <api/device.hpp>
#include <api/buffer.hpp>
#include <api/kernel.hpp>

int main(int argc, char* argv[]) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " <BDF> <vrtbin file>" << std::endl;
            return 1;
        }
        std::string bdf = argv[1];
        std::string vrtbinFile = argv[2];
        uint32_t size = 1024;
        vrt::utils::Logger::setLogLevel(vrt::utils::LogLevel::DEBUG);
        std::cout << "VRT Version: " << vrt::getVersion() << std::endl;
        vrt::Device device(bdf, vrtbinFile);

        vrt::Kernel accumulate(device, "accumulate_0");
        vrt::Kernel increment(device, "increment_0");
        vrt::Buffer<float> buffer(device, size, vrt::MemoryRangeType::HBM);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        float goldenModel = 0;
        std::cout << "Generating data...\n";
        for(uint32_t i = 0; i < size; i++) {
            buffer[i] = static_cast<float>(dis(gen));
            goldenModel += buffer[i] + 1;
        }

        buffer.sync(vrt::SyncType::HOST_TO_DEVICE);
        increment.start(size, buffer.getPhysAddr());
        accumulate.start(size);
        auto start = std::chrono::high_resolution_clock::now();
        increment.wait();
        accumulate.wait();
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "Time taken for waits: " << duration << " us" << std::endl;

        uint32_t val = accumulate.read(0x18);
        float floatVal;
        std::memcpy(&floatVal, &val, sizeof(float));
        if(std::fabs(goldenModel - floatVal) > 0.0001) {
            std::cerr << "Test failed!" << std::endl;
            std::cout << "Expected: " << goldenModel << std::endl;
            std::cout << "Got: " << floatVal << std::endl;
            device.cleanup();
            return 1;
        } else {
            std::cout << "Expected: " << goldenModel << std::endl;
            std::cout << "Got: " << floatVal << std::endl;
            std::cout << "Test passed!" << std::endl;
        }
        
        device.cleanup();

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    } 
    return 0;
}
