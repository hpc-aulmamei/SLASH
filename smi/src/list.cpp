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
#include <optional>
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

/// PCI device ID for the V80 QDMA function (PF1).
constexpr unsigned int SLASH_PF1_DEVICE_ID{0x50B5};

/// PCI device ID for the V80 control function (PF2).
constexpr unsigned int SLASH_PF2_DEVICE_ID{0x50B6};

/// Expected driver for PF0 (AMI management function).
constexpr char PF0_EXPECTED_DRIVER[] = "ami";

/// Expected driver for PF1 and PF2 (SLASH kernel module).
constexpr char SLASH_EXPECTED_DRIVER[] = "slash";


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
// PfStatus — per-physical-function readiness check
// ---------------------------------------------------------------------------

/// @brief Result of checking one physical function's readiness.
struct PfStatus {
    int         pfNumber{};  ///< Physical function number (0, 1, or 2).
    std::string bdf;         ///< Full BDF address of this PF.
    bool        ok{};        ///< True if the PF passes all checks.
    std::string reason;      ///< Empty when ok; describes the failure otherwise.
};

/// Checks whether a given PCI physical function exists, has the expected
/// device ID, and has the expected driver bound.
///
/// @param bdf              Full BDF string, e.g. "0000:03:00.1".
/// @param pfNumber         PF index (0, 1, or 2).
/// @param expectedDeviceId PCI device ID this PF should report.
/// @param expectedDriver   Kernel driver name that should be bound.
/// @return PfStatus with ok=true if all checks pass, or ok=false with reason.
static PfStatus checkPf(const std::string& bdf, int pfNumber,
                         unsigned int expectedDeviceId,
                         const char* expectedDriver) {
    std::filesystem::path devPath = PCI_DEVICES_PATH / bdf;

    if (!std::filesystem::exists(devPath)) {
        return {.pfNumber = pfNumber, .bdf = bdf, .ok = false, .reason = "not found"};
    }

    auto deviceId = readNumFile<unsigned int>(devPath / "device");
    if (deviceId != expectedDeviceId) {
        return {.pfNumber = pfNumber, .bdf = bdf, .ok = false, .reason = "bad device ID"};
    }

    std::string driver;
    {
        std::filesystem::path driverLink = devPath / "driver";
        if (std::filesystem::is_symlink(driverLink)) {
            driver = std::filesystem::read_symlink(driverLink).filename().string();
        }
    }

    if (driver != expectedDriver) {
        std::string actual = driver.empty() ? "(none)" : driver;
        return {
            .pfNumber = pfNumber,
            .bdf = bdf,
            .ok = false,
            .reason = "wanted driver: '" + std::string(expectedDriver) +
                      "', currently loaded driver: '" + actual + "'",
        };
    }

    return {.pfNumber = pfNumber, .bdf = bdf, .ok = true};
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
// V80Board — aggregated board-level readiness
// ---------------------------------------------------------------------------

/// @brief Aggregated readiness status of one V80 board (PF0 + PF1 + PF2).
struct V80Board {
    std::string bdfBase;    ///< BDF prefix without function digit, e.g. "0000:03:00".
    PfStatus    pf0;        ///< Status of PF0 (AMI management).
    PfStatus    pf1;        ///< Status of PF1 (QDMA).
    PfStatus    pf2;        ///< Status of PF2 (control).
    bool        longPrinting{};  ///< If true, include detailed sysfs info per PF.

    /// Detailed sysfs snapshot for each PF (populated only when longPrinting).
    std::optional<PciDevice> pf0Device;
    std::optional<PciDevice> pf1Device;
    std::optional<PciDevice> pf2Device;

    /// True when all three PFs are ready.
    bool ok() const { return pf0.ok && pf1.ok && pf2.ok; }
};

/// Tries to read a PciDevice from sysfs for the given BDF.
/// Returns std::nullopt if the sysfs path does not exist.
static std::optional<PciDevice> tryReadDevice(const std::string& bdf, bool longPrinting) {
    std::filesystem::path devPath = PCI_DEVICES_PATH / bdf;
    if (!std::filesystem::exists(devPath)) {
        return std::nullopt;
    }
    return PciDevice::fromDevPath(devPath, longPrinting);
}

/// Discovers V80 boards by scanning for PF0 devices, then checking PF1 and PF2.
///
/// @param longPrinting If true, also reads detailed sysfs attributes for each PF.
static std::vector<V80Board> discoverBoards(bool longPrinting) {
    auto pf0Devices = findPciDevices(SLASH_VENDOR_ID, SLASH_DEVICE_ID,
                                      SLASH_PF_NUMBER, /*longPrinting=*/false);

    std::vector<V80Board> boards;
    boards.reserve(pf0Devices.size());

    for (const auto& pf0Dev : pf0Devices) {
        std::string base = pf0Dev.bdf.substr(0, pf0Dev.bdf.rfind('.'));
        std::string pf1Bdf = base + ".1";
        std::string pf2Bdf = base + ".2";

        V80Board board{
            .bdfBase = base,
            .pf0 = checkPf(pf0Dev.bdf, 0, SLASH_DEVICE_ID, PF0_EXPECTED_DRIVER),
            .pf1 = checkPf(pf1Bdf, 1, SLASH_PF1_DEVICE_ID, SLASH_EXPECTED_DRIVER),
            .pf2 = checkPf(pf2Bdf, 2, SLASH_PF2_DEVICE_ID, SLASH_EXPECTED_DRIVER),
            .longPrinting = longPrinting,
        };

        if (longPrinting) {
            board.pf0Device = tryReadDevice(pf0Dev.bdf, true);
            board.pf1Device = tryReadDevice(pf1Bdf, true);
            board.pf2Device = tryReadDevice(pf2Bdf, true);
        }

        boards.push_back(std::move(board));
    }

    return boards;
}

// ---------------------------------------------------------------------------
// V80Board text output
// ---------------------------------------------------------------------------

/// Writes a single PF's status in parenthesized form.
static void printPfStatus(std::ostream& out, const PfStatus& pf) {
    out << "(PF" << pf.pfNumber << ": ";
    if (pf.ok) {
        out << "OK";
    } else {
        out << "NOT READY: " << pf.reason;
    }
    out << ")";
}

/// Prints the long-form device details for a PF, indented under the board.
static void printPfDetail(std::ostream& out, const PfStatus& pf,
                           const std::optional<PciDevice>& device) {
    out << INDENT1 << "PF" << pf.pfNumber << " " << pf.bdf << ": ";
    if (pf.ok) {
        out << "OK";
    } else {
        out << "NOT READY: " << pf.reason;
    }
    out << "\n";

    if (device) {
        const auto& dev = *device;
        out << INDENT2 << "Vendor ID: " << toHexString(dev.vendorId) << "\n"
            << INDENT2 << "Device ID: " << toHexString(dev.deviceId) << "\n"
            << INDENT2 << "Class: " << toHexString(dev.classCode) << "\n"
            << INDENT2 << "Subsystem vendor: " << toHexString(dev.subsysVendor) << "\n"
            << INDENT2 << "Subsystem device: " << toHexString(dev.subsysDevice) << "\n"
            << INDENT2 << "NUMA node: " << dev.numaNode << "\n"
            << INDENT2 << "Driver: " << (dev.driver.empty() ? "(none)" : dev.driver) << "\n"
            << INDENT2 << "IRQ: " << dev.irq << "\n"
            << INDENT2 << "Enabled: " << (dev.enabled ? "yes" : "no") << "\n"
            << INDENT2 << "Local CPUs: " << dev.localCpulist << "\n";
    }
}

/// Human-readable output for one V80 board.
/// In short mode, prints a single summary line.  In long mode, also
/// prints detailed sysfs attributes for each PF.
std::ostream& operator<<(std::ostream& out, const V80Board& board) {
    out << "Board " << board.bdfBase << ": "
        << (board.ok() ? "OK" : "NOT READY") << " ";
    printPfStatus(out, board.pf0);
    out << " ";
    printPfStatus(out, board.pf1);
    out << " ";
    printPfStatus(out, board.pf2);
    out << "\n";

    if (board.longPrinting) {
        printPfDetail(out, board.pf0, board.pf0Device);
        printPfDetail(out, board.pf1, board.pf1Device);
        printPfDetail(out, board.pf2, board.pf2Device);
    }

    return out;
}

/// Human-readable output for a list of V80 boards.
std::ostream& operator<<(std::ostream& out, const std::vector<V80Board>& boards) {
    for (const auto& board : boards) {
        out << board;
    }
    return out;
}

// ---------------------------------------------------------------------------
// V80Board JSON output
// ---------------------------------------------------------------------------

/// JSON representation of a single PF status.
/// If @p device is provided, its sysfs attributes are merged in.
static Json::Value pfToJson(const PfStatus& pf,
                             const std::optional<PciDevice>& device) {
    Json::Value j;
    j["bdf"] = pf.bdf;
    j["status"] = pf.ok ? "OK" : "NOT READY";
    if (!pf.ok) {
        j["reason"] = pf.reason;
    }
    if (device) {
        const auto& dev = *device;
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

/// JSON representation of a V80 board.
Json::Value toJson(const V80Board& board) {
    Json::Value j;
    j["bdf_base"] = board.bdfBase;
    j["status"] = board.ok() ? "OK" : "NOT READY";
    j["pf0"] = pfToJson(board.pf0, board.pf0Device);
    j["pf1"] = pfToJson(board.pf1, board.pf1Device);
    j["pf2"] = pfToJson(board.pf2, board.pf2Device);
    return j;
}

/// JSON representation of a list of V80 boards.
Json::Value toJson(const std::vector<V80Board>& boards) {
    Json::Value j;
    j["boards"] = Json::Value(Json::arrayValue);
    for (const auto& board : boards) {
        j["boards"].append(toJson(board));
    }
    return j;
}

// ---------------------------------------------------------------------------
// Command entry-point
// ---------------------------------------------------------------------------

/// Discovers all V80 boards and prints their readiness status.
///
/// In default mode, prints a one-line summary per board.  In long mode
/// (`-l`), also prints detailed sysfs attributes for each PF.
int List::run(const Options& options) {
    auto boards = discoverBoards(options.longOutput);
    print(boards, options.jsonOutput, options.prettyJsonOutput);

    return 0;
}
