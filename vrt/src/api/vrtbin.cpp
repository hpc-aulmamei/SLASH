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

#include "api/vrtbin.hpp"

#include <tar.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace vrt {

namespace {

constexpr std::size_t TAR_BLOCK_SIZE = 512;
constexpr char TAR_LONGNAME_TYPE = 'L';

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};
static_assert(sizeof(TarHeader) == TAR_BLOCK_SIZE, "Invalid tar header size");

bool isZeroBlock(const std::array<char, TAR_BLOCK_SIZE>& block) {
    return std::all_of(block.begin(), block.end(), [](char c) { return c == '\0'; });
}

uint64_t parseOctal(const char* field, std::size_t len) {
    uint64_t value = 0;
    bool seenDigit = false;
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(field[i]);
        if (c == '\0' || c == ' ') {
            if (seenDigit) {
                break;
            }
            continue;
        }
        if (c < '0' || c > '7') {
            break;
        }
        seenDigit = true;
        value = (value << 3) | static_cast<uint64_t>(c - '0');
    }
    return value;
}

std::string readField(const char* field, std::size_t len) {
    std::size_t n = 0;
    while (n < len && field[n] != '\0') {
        ++n;
    }
    return std::string(field, field + n);
}

void streamSkip(std::istream& stream, uint64_t size) {
    static constexpr std::streamsize CHUNK = 1 << 20;
    while (size > 0) {
        const std::streamsize chunk =
            static_cast<std::streamsize>(std::min<uint64_t>(size, static_cast<uint64_t>(CHUNK)));
        stream.ignore(chunk);
        if (stream.gcount() != chunk) {
            throw std::runtime_error("Unexpected EOF while skipping tar payload");
        }
        size -= static_cast<uint64_t>(chunk);
    }
}

void streamCopy(std::istream& src, std::ostream& dst, uint64_t size) {
    std::array<char, 1 << 16> buffer{};
    while (size > 0) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<uint64_t>(size, static_cast<uint64_t>(buffer.size())));
        src.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (src.gcount() != static_cast<std::streamsize>(chunk)) {
            throw std::runtime_error("Unexpected EOF while reading tar entry");
        }
        dst.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!dst) {
            throw std::runtime_error("Failed writing extracted tar entry");
        }
        size -= static_cast<uint64_t>(chunk);
    }
}

std::filesystem::path sanitizeArchivePath(const std::string& entryName) {
    std::filesystem::path path(entryName);
    path = path.lexically_normal();
    if (path.empty() || path == ".") {
        return {};
    }
    if (path.is_absolute()) {
        throw std::runtime_error("Tar archive contains absolute path entry: " + entryName);
    }
    for (const auto& part : path) {
        if (part == "..") {
            throw std::runtime_error("Tar archive contains parent path traversal: " + entryName);
        }
    }
    return path;
}

bool isRegularTarType(char typeflag) {
    return typeflag == REGTYPE || typeflag == AREGTYPE || typeflag == '\0';
}

}  // namespace

Vrtbin::Vrtbin(std::string vrtbinPath, const std::string& bdf) {
    this->vrtbinPath = vrtbinPath;
    if (!std::filesystem::exists(vrtbinPath)) {
        throw std::runtime_error(vrtbinPath + " does not exist");
    }

    const std::filesystem::path metadataPath =
        FilesystemCache::getCachePath() / ("metadata_" + sanitizeForPath(bdf));
    std::error_code metadataEc;
    std::filesystem::create_directories(metadataPath, metadataEc);
    if (metadataEc) {
        throw std::runtime_error("Failed to initialize metadata path: " + metadataPath.string());
    }

    this->tempExtractPath =
        (FilesystemCache::getCachePath() / ("vrtbin_" + sanitizeForPath(bdf))).string();
    this->systemMapPath = (metadataPath / "system_map.xml").string();
    this->versionPath = (metadataPath / "version.json").string();

    extract();
    discoverPdiFiles();

    const std::filesystem::path tempSystemMapPath = findExtractedFile("system_map.xml");
    if (tempSystemMapPath.empty()) {
        throw std::runtime_error("system_map.xml not found in tar archive: " + vrtbinPath);
    }
    XMLParser parser(tempSystemMapPath.string());
    parser.parseXML();
    this->platform = parser.getPlatform();
    copy(tempSystemMapPath.string(), systemMapPath);

    const std::filesystem::path versionJsonPath = findExtractedFile("version.json");
    if (!versionJsonPath.empty()) {
        copy(versionJsonPath.string(), versionPath);
    }

    const std::filesystem::path reportPath = findExtractedFile("report_utilization.xml");
    if (!reportPath.empty()) {
        copy(reportPath.string(), (metadataPath / "report_utilization.xml").string());
    }

    if (this->platform == Platform::HARDWARE) {
        if (pdiPaths.empty()) {
            throw std::runtime_error("No .pdi files found in tar archive: " + vrtbinPath);
        }
        extractUUID();
    } else if (this->platform == Platform::EMULATION) {
        const std::filesystem::path emuPath = findExtractedFile("vpp_emu");
        emulationExecPath = emuPath.empty() ? std::string() : emuPath.string();

    } else {
        const std::filesystem::path simPath = findExtractedFile("vpp_sim");
        simulationExecPath = simPath.empty() ? std::string() : simPath.string();
    }
}

void Vrtbin::extract() {
    utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__, "Extracting vrtbin: {}",
                       vrtbinPath);
    std::error_code ec;
    std::filesystem::remove_all(tempExtractPath, ec);
    std::filesystem::create_directories(tempExtractPath, ec);
    if (ec) {
        throw std::runtime_error("Failed to initialize extraction path: " + tempExtractPath);
    }

    std::ifstream archive(vrtbinPath, std::ios::binary);
    if (!archive.is_open()) {
        throw std::runtime_error("Cannot open tar archive: " + vrtbinPath);
    }

    std::string pendingLongName;
    while (true) {
        std::array<char, TAR_BLOCK_SIZE> raw{};
        archive.read(raw.data(), static_cast<std::streamsize>(raw.size()));
        if (archive.gcount() == 0) {
            break;
        }
        if (archive.gcount() != static_cast<std::streamsize>(raw.size())) {
            throw std::runtime_error("Invalid tar archive: truncated header");
        }
        if (isZeroBlock(raw)) {
            break;
        }

        TarHeader header{};
        std::memcpy(&header, raw.data(), sizeof(header));

        uint64_t payloadSize = parseOctal(header.size, sizeof(header.size));
        char typeflag = header.typeflag;
        std::string entryName;
        if (!pendingLongName.empty()) {
            entryName = pendingLongName;
            pendingLongName.clear();
        } else {
            std::string name = readField(header.name, sizeof(header.name));
            std::string prefix = readField(header.prefix, sizeof(header.prefix));
            entryName = prefix.empty() ? name : (prefix + "/" + name);
        }

        if (typeflag == TAR_LONGNAME_TYPE) {
            std::string longName(payloadSize, '\0');
            if (payloadSize > 0) {
                archive.read(longName.data(), static_cast<std::streamsize>(payloadSize));
                if (archive.gcount() != static_cast<std::streamsize>(payloadSize)) {
                    throw std::runtime_error("Invalid tar archive: truncated long name");
                }
            }
            std::size_t nul = longName.find('\0');
            if (nul != std::string::npos) {
                longName.resize(nul);
            }
            pendingLongName = longName;
            payloadSize = 0;
        } else {
            const std::filesystem::path relPath = sanitizeArchivePath(entryName);
            if (!relPath.empty()) {
                const std::filesystem::path outPath = std::filesystem::path(tempExtractPath) / relPath;
                if (typeflag == DIRTYPE) {
                    std::filesystem::create_directories(outPath);
                } else if (isRegularTarType(typeflag)) {
                    const auto parent = outPath.parent_path();
                    if (!parent.empty()) {
                        std::filesystem::create_directories(parent);
                    }
                    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                    if (!out.is_open()) {
                        throw std::runtime_error("Failed to create extracted file: " +
                                                 outPath.string());
                    }
                    streamCopy(archive, out, payloadSize);
                    payloadSize = 0;
                }
            }
        }

        if (payloadSize > 0) {
            streamSkip(archive, payloadSize);
        }
        const uint64_t padding = (TAR_BLOCK_SIZE - (parseOctal(header.size, sizeof(header.size)) % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
        if (padding > 0) {
            streamSkip(archive, padding);
        }
    }
}

void Vrtbin::copy(const std::string& source, const std::string& destination) {
    utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__, "Copying file {} to {}", source,
                       destination);
    std::ifstream src(source, std::ios::binary);
    if (!src) {
        utils::Logger::log(utils::LogLevel::ERROR, __PRETTY_FUNCTION__,
                           "Error opening source file: {}", source);
        throw std::runtime_error("Error opening source file");
    }

    std::ofstream dest(destination, std::ios::binary);
    if (!dest) {
        utils::Logger::log(utils::LogLevel::ERROR, __PRETTY_FUNCTION__,
                           "Error opening destination file: {}", destination);
        throw std::runtime_error("Error opening destination file");
    }

    dest << src.rdbuf();

    if (!src) {
        utils::Logger::log(utils::LogLevel::ERROR, __PRETTY_FUNCTION__,
                           "Error reading from source file: {}", source);
        throw std::runtime_error("Error reading from source file");
    }

    if (!dest) {
        utils::Logger::log(utils::LogLevel::ERROR, __PRETTY_FUNCTION__,
                           "Error writing to destination file: {}", destination);
        throw std::runtime_error("Error writing to destination file");
    }
}

std::string Vrtbin::getSystemMapPath() { return systemMapPath; }
std::string Vrtbin::getPdiPath() { return pdiPath; }
std::vector<std::string> Vrtbin::getPdiPaths() { return pdiPaths; }

std::string Vrtbin::getUUID() { return uuid; }

void Vrtbin::extractUUID() {
    utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__,
                       "Extracting UUID from version.json");
    const std::filesystem::path versionJsonPath = findExtractedFile("version.json");
    if (versionJsonPath.empty()) {
        uuid = "";
        return;
    }
    std::ifstream jsonFile(versionJsonPath);
    if (!jsonFile.is_open()) {
        uuid = "";
        return;
    }
    std::string line;
    while (std::getline(jsonFile, line)) {
        std::size_t pos = line.find("\"logic_uuid\":");
        if (pos != std::string::npos) {
            std::size_t start = line.find("\"", pos + 13) + 1;
            std::size_t end = line.find("\"", start);
            uuid = line.substr(start, end - start);
            break;
        }
    }
    utils::Logger::log(utils::LogLevel::DEBUG, __PRETTY_FUNCTION__, "UUID is: {}", uuid);
    jsonFile.close();
}

std::string Vrtbin::getEmulationExec() { return emulationExecPath; }

std::string Vrtbin::getSimulationExec() { return simulationExecPath; }

void Vrtbin::discoverPdiFiles() {
    pdiPaths.clear();

    if (!std::filesystem::exists(tempExtractPath)) {
        return;
    }

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(tempExtractPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".pdi") {
            pdiPaths.push_back(entry.path().string());
        }
    }

    std::sort(pdiPaths.begin(), pdiPaths.end());
    if (pdiPaths.empty()) {
        pdiPath.clear();
        return;
    }

    auto preferred = std::find_if(pdiPaths.begin(), pdiPaths.end(), [](const std::string& p) {
        return std::filesystem::path(p).filename() == "design.pdi";
    });
    if (preferred != pdiPaths.end() && preferred != pdiPaths.begin()) {
        std::iter_swap(pdiPaths.begin(), preferred);
    }
    pdiPath = pdiPaths.front();
}

std::filesystem::path Vrtbin::findExtractedFile(const std::string& filename) const {
    const std::filesystem::path direct = std::filesystem::path(tempExtractPath) / filename;
    if (std::filesystem::exists(direct)) {
        return direct;
    }
    if (!std::filesystem::exists(tempExtractPath)) {
        return {};
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(tempExtractPath)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) {
            return entry.path();
        }
    }
    return {};
}

std::string Vrtbin::sanitizeForPath(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? std::string("default") : out;
}

}  // namespace vrt
