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

#include "commands/reset_command.hpp"

ResetCommand::ResetCommand(const std::string& device) : device(device) {
    this->dev = nullptr;
    if (ami_dev_find(device.c_str(), &dev) != AMI_STATUS_OK) {
        std::cerr << "Error finding ami device: " << device << std::endl;
        throw std::runtime_error("Error finding device");
    }

    if (ami_dev_request_access(dev) != AMI_STATUS_OK) {
        throw std::runtime_error("Failed to request elevated access to device");
    }
}

void ResetCommand::execute() {
    PcieDriverHandler pcieDriverHandler(device + ":00.0");

    int ret = ami_prog_device_boot(&dev, 1);  // segmented PDI is on partition 1

    if (ret != AMI_STATUS_OK && geteuid() == 0) {
        throw std::runtime_error("Error booting device to partition 1");
    }

    ami_mem_bar_write(dev, 0, 0x1040000, 1);
    ami_dev_delete(&dev);
    pcieDriverHandler.execute(PcieDriverHandler::Command::REMOVE);
    pcieDriverHandler.execute(PcieDriverHandler::Command::TOGGLE_SBR);
    usleep(5000000);
    pcieDriverHandler.execute(PcieDriverHandler::Command::RESCAN);
    pcieDriverHandler.execute(PcieDriverHandler::Command::HOTPLUG);

    if (ami_dev_find(device.c_str(), &dev) != AMI_STATUS_OK) {
        std::cerr << "Error finding ami device: " << device << std::endl;
        throw std::runtime_error("Error finding device");
    }

    std::cout << "Device resetted successfully" << std::endl;
    ami_dev_delete(&dev);
}