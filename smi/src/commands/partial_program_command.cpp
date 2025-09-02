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

#include "commands/partial_program_command.hpp"

#include "utils/filesystem_cache.hpp"

PartialProgramCommand::PartialProgramCommand(const std::string& device,
                                             const std::string& image_path) {
    this->device = device;
    this->imagePath = image_path;
    this->dev = nullptr;
    if (ami_dev_find(device.c_str(), &dev) != AMI_STATUS_OK) {
        std::cerr << "Error finding ami device: " << device << std::endl;
        throw std::runtime_error("Error finding device");
    }

    if (ami_dev_request_access(dev) != AMI_STATUS_OK) {
        throw std::runtime_error("Failed to request elevated access to device");
    }
}

void PartialProgramCommand::execute() {
    PcieDriverHandler pcieDriverHandler(device + ":00.0");
    int found_current_uuid = AMI_STATUS_ERROR;
    int found_new_uuid = AMI_STATUS_ERROR;
    std::string new_uuid;
    char current_uuid[AMI_LOGIC_UUID_SIZE] = {0};
    uint16_t dev_bdf;
    ami_dev_get_pci_bdf(dev, &dev_bdf);

    if (ArgParser::endsWith(this->imagePath, ".vrtbin")) {
        Vrtbin::extract(this->imagePath, FilesystemCache::getCachePath());
        std::string ami_path = std::string(std::getenv("AMI_HOME"));
        std::string create_path = "mkdir -p " + ami_path + "/" + device + ":00.0/";
        std::string basePath = ami_path + "/" + device + ":00.0/";
        system(create_path.c_str());
        Vrtbin::copy(FilesystemCache::getCachePath() / "system_map.xml", basePath + "system_map.xml");
        Vrtbin::copy(FilesystemCache::getCachePath() / "version.json", basePath + "version.json");
        Vrtbin::copy(FilesystemCache::getCachePath() / "report_utilization.xml", basePath + "report_utilization.xml");
        imagePath = FilesystemCache::getCachePath() / "design.pdi";
    }

    int ret = ami_prog_device_boot(&dev, 1);  // segmented PDI is on partition 1

    if (ret != AMI_STATUS_OK && geteuid() == 0) {
        throw std::runtime_error("Error booting device to partition 1");
    }

    ami_mem_bar_write(dev, 0, 0x1040000,
                      1);  // PMC GPIO. this is needed for reset PDI into partition 1
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

    found_current_uuid = ami_dev_read_uuid(dev, current_uuid);
    found_new_uuid = Vrtbin::extractUUID().empty() ? AMI_STATUS_ERROR : AMI_STATUS_OK;
    new_uuid = Vrtbin::extractUUID().substr(0, 32);
    printf(
        "----------------------------------------------\r\n"
        "Device | %02x:%02x.%01x\r\n"
        "----------------------------------------------\r\n"
        "Current Configuration\r\n"
        "----------------------------------------------\r\n"
        "UUID   | %s\r\n"
        "----------------------------------------------\r\n"
        "Incoming Configuration\r\n"
        "----------------------------------------------\r\n"
        "UUID      | %s\r\n"
        "Path      | %s\r\n"
        "----------------------------------------------\r\n",
        AMI_PCI_BUS(dev_bdf), AMI_PCI_DEV(dev_bdf), AMI_PCI_FUNC(dev_bdf),
        ((found_current_uuid != AMI_STATUS_OK) ? ("N/A") : (current_uuid)),
        ((found_new_uuid != AMI_STATUS_OK) ? ("N/A") : (new_uuid.c_str())), imagePath.c_str());

    if (new_uuid == current_uuid) {
        std::cout << "Device configured with the same image.\n";
        ami_dev_delete(&dev);
        return;
    }

    if (ami_dev_request_access(dev) != AMI_STATUS_OK) {
        std::cerr << "Error requesting access to ami device: " << device << std::endl;
        throw std::runtime_error("Error requesting access to device");
    }

    if (ami_prog_download_pdi(dev, imagePath.c_str(), 0, 0, Vrtbin::progressHandler, true) !=
        AMI_STATUS_OK) {
        std::cerr << "Error downloading image to ami device: " << device << std::endl;
        throw std::runtime_error("Error downloading image to device");
    }
    ami_dev_delete(&dev);
    pcieDriverHandler.execute(PcieDriverHandler::Command::REMOVE);
    usleep(DELAY_PARTIAL_BOOT);
    pcieDriverHandler.execute(PcieDriverHandler::Command::RESCAN);

    if (ami_dev_find(device.c_str(), &dev) != AMI_STATUS_OK) {
        std::cerr << "Error finding ami device: " << device << std::endl;
        throw std::runtime_error("Error finding device");
    }

    found_current_uuid = ami_dev_read_uuid(dev, current_uuid);

    if (std::string(current_uuid) == new_uuid) {
        std::cout << "\nPartial Program Command executed successfully\n";
    } else {
        std::cerr << "Error: Partial Program Command failed\n";
        std::cerr << "Possible NoC configuration missmatch. Check V80 PLM logs\n";
    }
}