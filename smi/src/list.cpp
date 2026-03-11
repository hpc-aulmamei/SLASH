/**
 * The MIT License (MIT)
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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

/// @file list.cpp
/// @brief Implementation of the List command.
///
/// Discovers V80 devices by scanning /sys/bus/pci/devices for entries
/// whose vendor and device IDs match the Slash platform, then prints
/// them in short, long, or JSON format.

#include "list.hpp"

#include <limits>
#include <filesystem>
#include <fstream>
#include <vector>

#include "utils.hpp"

/// Root sysfs directory that contains one symlink per PCI device.
static const std::filesystem::path PCI_DEVICES_PATH{"/sys/bus/pci/devices"};

/// PCI vendor ID assigned to Slash/V80 devices (Xilinx).
constexpr unsigned int SLASH_VENDOR_ID{0x10EE};

/// PCI device ID for the V80 accelerator.
constexpr unsigned int SLASH_DEVICE_ID{0x50B4};

/// Physical Function number used by Slash on the V80.
/// Only PF0 is relevant; other functions belong to other subsystems.
constexpr unsigned int SLASH_PF_NUMBER{0};


// ---------------------------------------------------------------------------
// sysfs helpers
// ---------------------------------------------------------------------------

/// Reads a single numeric value from a sysfs file.
///
/// Most sysfs attribute files contain one value optionally prefixed with
/// "0x" and followed by a newline.  Returns sentinel<Int>() if the file
/// cannot be opened or parsed.
///
/// @tparam Int  Integral type to read into.
/// @tparam Hex  If true (default), parse as hexadecimal; otherwise decimal.
/// @param path  Absolute path to the sysfs attribute file.
template<class Int, bool Hex = true>
static Int readNumFile(const std::filesystem::path& path) {
    std::ifstream f{path};
    Int val{sentinel<Int>()};
    if (f.is_open()) {
        if constexpr (Hex) {
            f >> std::hex >> val;
        } else {
            f >> val;
        }
    }
    return val;
}

/// Reads a sysfs file as a single trimmed line.
///
/// Trailing whitespace and carriage returns are stripped.  Returns an
/// empty string if the file cannot be opened.
///
/// @param path Absolute path to the sysfs attribute file.
static std::string readStringFile(const std::filesystem::path& path) {
    std::ifstream f(path);
    std::string val;
    if (f.is_open()) {
        std::getline(f, val);
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) {
            val.pop_back();
        }
    }
    return val;
}

// ---------------------------------------------------------------------------
// PciDevice — snapshot of one PCI device's sysfs attributes
// ---------------------------------------------------------------------------

/// @brief Holds the sysfs attributes of a single PCI device.
///
/// All fields are populated once by fromDevPath() and are then read-only.
/// The @c longPrinting flag controls how much detail is shown in text and
/// JSON output — short mode prints only the BDF.
struct PciDevice {
    std::string           bdf;             ///< BDF address, e.g. "0000:03:00.0".
    std::filesystem::path sysfsPath;       ///< Full path under /sys/bus/pci/devices/.
    unsigned int          vendorId{};      ///< PCI vendor ID.
    unsigned int          deviceId{};      ///< PCI device ID.
    unsigned int          classCode{};     ///< 24-bit PCI class code.
    unsigned int          subsysVendor{};  ///< Subsystem vendor ID.
    unsigned int          subsysDevice{};  ///< Subsystem device ID.
    int                   numaNode{};      ///< NUMA node affinity (-1 if not applicable).
    std::string           driver;          ///< Currently bound kernel driver (empty if unbound).
    std::string           irq;             ///< IRQ number as reported by sysfs.
    bool                  enabled{};       ///< Whether the device is enabled (sysfs "enable" == "1").
    std::string           resource;        ///< First line of the resource file (BAR0 mapping).
    std::string           localCpulist;    ///< CPU list local to this device's NUMA node.
    bool                  longPrinting{};  ///< If true, output detailed info; otherwise BDF only.

    
    /// Constructs a PciDevice by reading all relevant sysfs attributes from
    /// @p devPath (e.g. /sys/bus/pci/devices/0000:03:00.0).
    static PciDevice fromDevPath(const std::filesystem::path& devPath, bool longPrinting) {
        std::string driver;
        {
            std::filesystem::path driverLink = devPath / "driver";
            if (std::filesystem::is_symlink(driverLink)) {
                driver = std::filesystem::read_symlink(driverLink).filename().string();
            }
        }

        return PciDevice{
            .bdf{devPath.filename().string()},
            .sysfsPath{devPath},
            .vendorId{readNumFile<unsigned int>(devPath / "vendor")},
            .deviceId{readNumFile<unsigned int>(devPath / "device")},
            .classCode{readNumFile<unsigned int>(devPath / "class")},
            .subsysVendor{readNumFile<unsigned int>(devPath / "subsystem_vendor")},
            .subsysDevice{readNumFile<unsigned int>(devPath / "subsystem_device")},
            .numaNode{readNumFile<int, false>(devPath / "numa_node")},
            .driver{std::move(driver)},
            .irq{readStringFile(devPath / "irq")},
            .enabled{readStringFile(devPath / "enable") == "1"},
            .resource{readStringFile(devPath / "resource")},
            .localCpulist{readStringFile(devPath / "local_cpulist")},
            .longPrinting{longPrinting},
        };
    }
};



// ---------------------------------------------------------------------------
// PciDevice text output
// ---------------------------------------------------------------------------

/// Human-readable output for a single PCI device.
/// In short mode, prints only the BDF; in long mode, prints all attributes.
std::ostream& operator<<(std::ostream& out, const PciDevice& dev) {
    if (!dev.longPrinting) {
        return out << dev.bdf << "\n";
    } else {
        out
            << "Device " << dev.bdf << ":\n"
            << INDENT1 << "Vendor ID: " << toHexString(dev.vendorId) << "\n"
            << INDENT1 << "Device ID: " << toHexString(dev.deviceId) << "\n"
            << INDENT1 << "Class: " << toHexString(dev.classCode) << "\n"
            << INDENT1 << "Subsystem vendor: " << toHexString(dev.subsysVendor) << "\n"
            << INDENT1 << "Subsystem device: " << toHexString(dev.subsysDevice) << "\n"
            << INDENT1 << "NUMA node: " << dev.numaNode << "\n"
            << INDENT1 << "Driver: " << (dev.driver.empty() ? "(none)" : dev.driver) << "\n"
            << INDENT1 << "IRQ: " << dev.irq << "\n"
            << INDENT1 << "Enabled: " << (dev.enabled ? "yes" : "no") << "\n"
            << INDENT1 << "Local CPUs: " << dev.localCpulist << "\n";
    }

    return out;
}

/// Outputs a vector of PCI devices sequentially.
std::ostream& operator<<(std::ostream& out, const std::vector<PciDevice>& devices) {
    for (const auto& dev : devices) {
        out << dev;
    }

    return out;
}

// ---------------------------------------------------------------------------
// PciDevice JSON output
// ---------------------------------------------------------------------------

/// JSON representation of a single PCI device.
/// In short mode only the BDF is included; long mode adds all attributes.
Json::Value toJson(const PciDevice& dev) {
    Json::Value j;

    j["bdf"] = dev.bdf;

    if (dev.longPrinting) {
        j["vendor_id"] = toHexString(dev.vendorId);
        j["device_id"] = toHexString(dev.deviceId);
        j["class"] = toHexString(dev.classCode);
        j["subsystem_vendor"] = toHexString(dev.subsysVendor);
        j["subsystem_device"] = toHexString(dev.subsysDevice);
        j["numa_node"] = toHexString(static_cast<unsigned int>(dev.numaNode));
        j["driver"] = dev.driver;
        j["irq"] = dev.irq;
        j["enabled"] = dev.enabled;
        j["local_cpulist"] = dev.localCpulist;
    }

    return j;
}

/// JSON representation of a list of PCI devices, wrapped in a
/// `{ "devices": [ ... ] }` object.
Json::Value toJson(const std::vector<PciDevice>& devices) {
    Json::Value j;

    j["devices"] = Json::Value(Json::arrayValue);

    for (const auto &dev : devices) {
        j["devices"].append(toJson(dev));
    }

    return j;
}

// ---------------------------------------------------------------------------
// PCI device discovery
// ---------------------------------------------------------------------------

/// Scans sysfs for PCI devices matching a vendor/device ID, optionally
/// filtered to a specific Physical Function number.
///
/// The PF number corresponds to the function digit in the BDF string
/// ("DDDD:BB:DD.**F**").  Pass sentinel<int>() to skip PF filtering.
///
/// @param vendorId     PCI vendor ID to match.
/// @param deviceId     PCI device ID to match.
/// @param pfNumber     Physical Function number to require (0–7), or
///                     sentinel to accept any function.
/// @param longPrinting Forwarded to PciDevice; controls output verbosity.
/// @return             Vector of matching PciDevice snapshots.
/// @throws std::out_of_range if pfNumber is outside 0–7 (and not sentinel).
std::vector<PciDevice> findPciDevices(unsigned int vendorId, unsigned int deviceId, int pfNumber, bool longPrinting) {
    std::vector<PciDevice> results;

    // Build the BDF suffix filter, e.g. ".0" for PF0.
    std::string suffix;
    if (!isSentinel(pfNumber)) {
        if (pfNumber < 0 || pfNumber > 7) {
            throw std::out_of_range("PF Number out of range");
        }

        suffix = "." + std::to_string(pfNumber);
    }

    for (const auto& entry : std::filesystem::directory_iterator(PCI_DEVICES_PATH)) {
        std::string bdf{entry.path().filename().string()};

        // BDF must end with the required PF suffix.
        if (!bdf.ends_with(suffix)) {
            continue;
        }

        auto vendor{readNumFile<unsigned int>(entry.path() / "vendor")};
        auto device{readNumFile<unsigned int>(entry.path() / "device")};

        if (vendor == vendorId && device == deviceId) {
            results.push_back(PciDevice::fromDevPath(entry.path(), longPrinting));
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Command entry-point
// ---------------------------------------------------------------------------

/// Discovers all V80 devices on the PCI bus and prints them.
int List::run(const Options& options) {
    auto devices{findPciDevices(SLASH_VENDOR_ID, SLASH_DEVICE_ID, SLASH_PF_NUMBER, options.longOutput)};

    print(devices, options.jsonOutput, options.prettyJsonOutput);

    return 0;
}
