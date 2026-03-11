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

/// @file inspect.cpp
/// @brief Implementation of the Inspect (and Query) command.
///
/// Reads vbin metadata - either from a vbin file on disk or from the
/// system-map of whatever was last loaded on a device - and prints
/// kernel information (name, physical address, arguments) in text or JSON.

#include "inspect.hpp"

#include <charconv>
#include <iostream>
#include <map>
#include <sstream>

#include <vrt/vrtbin.hpp>

#include "utils.hpp"

//. BDF string corresponding to the all-ones sentinel value (0xFFFF).
///
/// Passed to vrt::Vrtbin when we're inspecting a file and have no real device.
/// This will only determine the name of the path where the vbin is extracted.
/// This BDF should never occur in reality.
constexpr char BDF_SENTINEL[] = "FF:1F.7";


// ---------------------------------------------------------------------------
// Direction helpers
// ---------------------------------------------------------------------------

/// Converts readable/writable flags into a human-readable direction string.
/// @return "Read", "Write", "ReadWrite", or "" if neither flag is set.
std::string directionToString(bool readable, bool writable) {
    std::stringstream ss;

    ss << (readable ? "Read" : "") << (writable ? "Write" : "");

    return ss.str();
}

/// Convenience overload that extracts the flags from a FunctionalArg.
std::string directionToString(const vrt::FunctionalArg& arg) {
    return directionToString(arg.readable, arg.writable);
}

// ---------------------------------------------------------------------------
// FunctionalArg formatting (text & JSON)
// ---------------------------------------------------------------------------

/// Human-readable output for a single kernel argument.
std::ostream& operator<<(std::ostream& out, const vrt::FunctionalArg& arg) {
    return out
        << INDENT2 << "Argument:\n"
        << INDENT3 << "Index: " << arg.idx << "\n"
        << INDENT3 << "Name: " << arg.name << "\n"
        << INDENT3 << "Type: " << arg.type << "\n"
        << INDENT3 << "Offset: " << arg.offset << "\n"
        << INDENT3 << "Range: " << arg.range << "\n"
        << INDENT3 << "Direction: " << directionToString(arg) << "\n";
}

/// JSON representation of a single kernel argument.
Json::Value toJson(const vrt::FunctionalArg& arg) {
    Json::Value j;

    j["index"] = toHexString(arg.idx);
    j["name"] = arg.name;
    j["type"] = arg.type;
    j["offset"] = toHexString(arg.offset); // Prevent JSON number issues
    j["range"] = toHexString(arg.range); // Prevent JSON number issues
    j["direction"] = directionToString(arg);

    return j;
}

// ---------------------------------------------------------------------------
// KernelData — lightweight snapshot of a vrt::Kernel
// ---------------------------------------------------------------------------

/// @brief Holds the subset of vrt::Kernel data needed for display.
struct KernelData {
    std::string name;                       ///< Kernel name from the system map.
    uint64_t physAddress{};                 ///< Physical (mapped) address of the kernel.
    std::vector<vrt::FunctionalArg> args;   ///< HLS functional arguments.

    /// Extracts display-relevant data from a live vrt::Kernel object.
    static KernelData fromKernel(const vrt::Kernel& kernel) {
        return KernelData {
            .name{kernel.getName()},
            .physAddress{kernel.getPhysAddr()},
            .args{kernel.getFunctionalArgs()},
        };
    }
};

/// Human-readable output for a kernel and its arguments.
std::ostream& operator<<(std::ostream& out, const KernelData& kernel) {
    out
        << INDENT1 << "Kernel:\n"
        << INDENT2 << "Name: " << kernel.name << "\n"
        << INDENT2 << "Physical address: " << toHexString(kernel.physAddress) << "\n";

    for (const auto& arg : kernel.args) {
        out << arg;
    }

    return out;
}

/// JSON representation of a kernel and its arguments.
Json::Value toJson(const KernelData& kernel) {
    Json::Value j;

    j["name"] = kernel.name;
    j["address"] = toHexString(kernel.physAddress);

    if (!kernel.args.empty()) {
        j["args"] = Json::Value(Json::arrayValue);

        for (const auto& arg : kernel.args) {
            j["args"].append(toJson(arg));
        }
    }
    
    return j;
}

// ---------------------------------------------------------------------------
// VbinData — lightweight snapshot of a whole vbin / system-map
// ---------------------------------------------------------------------------

/// @brief Holds the metadata extracted from a vbin or a device's system map.
struct VbinData {
    std::string name{};                              ///< Display label (file path or "on <BDF>").
    vrt::Platform platform{vrt::Platform::UNKNOWN};  ///< Target platform (HW / emulation / sim).
    uint64_t clockFrequency{};                       ///< Design clock frequency in Hz.
    std::map<std::string, KernelData> kernels;       ///< Kernels keyed by name.

    /// Builds a VbinData from an already-parsed system-map XMLParser.
    static VbinData fromParser(vrt::XMLParser& parser, const std::string& name) {
        std::map<std::string, KernelData> kernels;

        for (const auto& [kernelName, kernel] : parser.getKernels()) {
            kernels.emplace(kernelName, KernelData::fromKernel(kernel));
        }

        return VbinData {
            .name{name},
            .platform{parser.getPlatform()},
            .clockFrequency{parser.getClockFrequency()},
            .kernels{std::move(kernels)},
        };
    }

    /// Builds a VbinData from a vrt::Vrtbin that has already been opened.
    static VbinData fromVbin(vrt::Vrtbin& vbin, const std::string& name) {  
        vrt::XMLParser parser{vbin.getSystemMapPath()};
        parser.parseXML();

        return fromParser(parser, name);
    }

    /// Builds a VbinData by querying the system map currently loaded on
    /// the device at the given BDF address.
    static VbinData fromBdf(const std::string& bdf) {
        vrt::XMLParser parser{vrt::Vrtbin::getSystemMapPathFromBdf(bdf)};
        parser.parseXML();

        return fromParser(parser, "on " + bdf);
    }

    /// Builds a VbinData by extracting and parsing a vbin file on disk.
    static VbinData fromPath(const std::string& path) {
        // BDF_SENTINEL is used because Vrtbin requires a BDF string even
        // when we only need to inspect the file contents, not target a device.
        vrt::Vrtbin vbin{path, BDF_SENTINEL};
        vbin.extract();

        return fromVbin(vbin, path);
    }
};

/// Converts a vrt::Platform enum to its string name.
const char* toString(vrt::Platform platform) {
    switch (platform) {
    case vrt::Platform::HARDWARE:
        return "HARDWARE";
    case vrt::Platform::EMULATION:
        return "EMULATION";
    case vrt::Platform::SIMULATION:
        return "SIMULATION";
    default:
        return "UNKNOWN";
    }
}

/// Human-readable output for an entire vbin's metadata.
std::ostream& operator<<(std::ostream& out, const VbinData& vbin) {
    out
        << "Vbin " << vbin.name << ":\n"
        << INDENT1 << "Platform: " << toString(vbin.platform) << "\n"
        << INDENT1 << "Clock frequency: " << vbin.clockFrequency << "\n";

    for (const auto& [_, kernel] : vbin.kernels) {
        out << kernel;
    }

    return out;
}

/// JSON representation of an entire vbin's metadata.
Json::Value toJson(const VbinData& vbin) {
    Json::Value j;

    j["clock_frequency"] = toHexString(vbin.clockFrequency);

    if (!vbin.kernels.empty()) {
        j["kernels"] = Json::Value{};

        for (const auto& [name, kernel] : vbin.kernels) {
            j["kernels"][name] = toJson(kernel);
        }
    }
    
    return j;
}

// ---------------------------------------------------------------------------
// Command entry-point
// ---------------------------------------------------------------------------

/// Loads and reads the data source (BDF query or file path) based on the options.
VbinData getVbinData(const Inspect::Options& options) {
    if (options.isBdfQuery) {
        return VbinData::fromBdf(options.bdf);
    } else {
        return VbinData::fromPath(options.vbinPath);
    }
}

/// Runs the inspect/query command: loads vbin metadata and prints it.
int Inspect::run(const Options& options) {
    const auto vbinData{getVbinData(options)};

    print(vbinData, options.jsonOutput, options.prettyJsonOutput);

    return 0;
}
