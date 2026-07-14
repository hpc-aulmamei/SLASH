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

/// @file write_static_shell.cpp
/// @brief Implementation of the write-static-shell command.

#include "write_static_shell.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <vrtd/session.hpp>

#include "bdf.hpp"

#ifndef SMI_VERSAL_FLASH_TCL
#define SMI_VERSAL_FLASH_TCL "/usr/share/v80-smi/versal_flash_pdi.tcl"
#endif

namespace {

constexpr uint8_t StaticShellBootDevice = 0;
constexpr uint32_t StaticShellPartition = 0;

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

std::string trimTrailingNewline(std::string text) {
    const auto end = text.find_last_not_of("\r\n");
    if (end == std::string::npos) {
        return "";
    }
    text.erase(end + 1);
    return text;
}

std::string shellQuote(const std::string& text) {
    std::string quoted = "'";
    for (char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string resolveStaticShellPdi(bool nofpt) {
    std::string command = "python3 -m slashkit static-shell-path";
    if (nofpt) {
        command += " --nofpt";
    }

    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error(
            "Failed to run 'python3 -m slashkit static-shell-path': " +
            std::string(std::strerror(errno)));
    }

    std::string output;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status == -1) {
        throw std::runtime_error(
            "Failed to wait for 'python3 -m slashkit static-shell-path': " +
            std::string(std::strerror(errno)));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("'python3 -m slashkit static-shell-path' failed");
    }

    std::string path = trimTrailingNewline(output);
    if (path.empty()) {
        throw std::runtime_error("'python3 -m slashkit static-shell-path' printed no path");
    }
    if (!fileExists(path)) {
        throw std::runtime_error("Static shell PDI path does not exist: " + path);
    }

    return path;
}

std::string resolvePdiPath(const WriteStaticShell::Options& options, bool nofpt) {
    if (!options.pdiPath.empty()) {
        if (!fileExists(options.pdiPath)) {
            throw std::runtime_error("PDI path does not exist: " + options.pdiPath);
        }
        return options.pdiPath;
    }

    return resolveStaticShellPdi(nofpt);
}

std::string resolveVersalFlashTcl() {
    if (const char *overridePath = std::getenv("SMI_VERSAL_FLASH_TCL")) {
        if (overridePath[0] != '\0') {
            return overridePath;
        }
    }

    return SMI_VERSAL_FLASH_TCL;
}

std::string buildXsdbCommand(const std::vector<std::string>& bashSources,
                             const std::string& tclPath) {
    std::string command;
    for (const auto& source : bashSources) {
        command += "source " + shellQuote(source) + "; ";
    }
    command += "xsdb " + shellQuote(tclPath);
    return command;
}

void runBashCommandWithPdiPath(const std::string& command,
                               const std::string& pdiPath) {
    const pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        if (setenv("PDI_PATH", pdiPath.c_str(), 1) != 0) {
            std::perror("setenv PDI_PATH");
            _exit(127);
        }
        execl("/bin/bash", "/bin/bash", "-c", command.c_str(),
              static_cast<char *>(nullptr));
        std::perror("execl /bin/bash");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
    }

    if (!WIFEXITED(status)) {
        throw std::runtime_error("xsdb command did not exit normally");
    }
    if (WEXITSTATUS(status) != 0) {
        throw std::runtime_error(
            "xsdb command failed with exit code " + std::to_string(WEXITSTATUS(status)));
    }
}

void validateOptions(const WriteStaticShell::Options& options) {
    if (!options.jtag) {
        if (options.bdf.empty()) {
            throw std::invalid_argument("-d/--device is required for --flash");
        }
        if (options.noDevice) {
            throw std::invalid_argument("--no-device is only valid with --jtag");
        }
        if (!options.bashSources.empty()) {
            throw std::invalid_argument("--bash-source is only valid with --jtag");
        }
        return;
    }

    if (options.noDevice && !options.bdf.empty()) {
        throw std::invalid_argument("--no-device cannot be used with -d/--device");
    }
    if (!options.noDevice && options.bdf.empty()) {
        throw std::invalid_argument("-d/--device is required for --jtag unless --no-device is used");
    }
}

int runFlashMode(const WriteStaticShell::Options& options) {
    const std::string bdf = resolveBoardBdf(options.bdf, "write-static-shell");
    const std::string pdiPath = resolvePdiPath(options, false);

    vrtd::Session session;
    auto device = session.getDeviceByBdf(bdf);
    device.cfgmemProgramFile(pdiPath, StaticShellBootDevice, StaticShellPartition);

    return 0;
}

void rethrowXsdbAndRescanErrors(std::exception_ptr xsdbError,
                                const std::exception& rescanError) {
    try {
        if (xsdbError) {
            std::rethrow_exception(xsdbError);
        }
    } catch (const std::exception& xsdbException) {
        throw std::runtime_error(
            std::string("xsdb command failed: ") + xsdbException.what() +
            "; hotplug rescan also failed: " + rescanError.what());
    } catch (...) {
        throw std::runtime_error(
            std::string("xsdb command failed with an unknown error; "
                        "hotplug rescan also failed: ") + rescanError.what());
    }

    throw std::runtime_error("hotplug rescan failed: " + std::string(rescanError.what()));
}

int runJtagMode(const WriteStaticShell::Options& options) {
    const std::string pdiPath = resolvePdiPath(options, true);
    const std::string tclPath = resolveVersalFlashTcl();
    if (!fileExists(tclPath)) {
        throw std::runtime_error("versal_flash_pdi.tcl path does not exist: " + tclPath);
    }

    vrtd::Session session;
    if (!options.noDevice) {
        const std::string bdf = resolveBoardBdf(options.bdf, "write-static-shell");
        auto device = session.getDeviceByBdf(bdf);
        device.hotplugOp(vrtd::HotplugOp::Remove, vrtd::HotplugFunctionAll);
    }

    std::exception_ptr xsdbError;
    try {
        runBashCommandWithPdiPath(
            buildXsdbCommand(options.bashSources, tclPath), pdiPath);
    } catch (...) {
        xsdbError = std::current_exception();
    }

    try {
        session.hotplugRescan();
    } catch (const std::exception& e) {
        rethrowXsdbAndRescanErrors(xsdbError, e);
    }

    if (xsdbError) {
        std::rethrow_exception(xsdbError);
    }

    return 0;
}

} // namespace

int WriteStaticShell::run(const Options& options) {
    validateOptions(options);

    if (options.jtag) {
        return runJtagMode(options);
    }

    return runFlashMode(options);
}
