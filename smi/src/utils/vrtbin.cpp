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

#include "utils/vrtbin.hpp"
#include "utils/filesystem_cache.hpp"

void Vrtbin::extract(std::string source, std::string destination) {
    std::string command = "tar -xvf " + source + " -C " + destination + " 2>&1";
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
}

void Vrtbin::copy(const std::string& source, const std::string& destination) {
    std::ifstream src(source, std::ios::binary);
    if (!src) {
        std::cerr << "Error opening source file: " << source << std::endl;
        throw std::runtime_error("Error opening source file");
    }

    std::ofstream dest(destination, std::ios::binary);
    if (!dest) {
        std::cerr << "Error opening destination file: " << destination << std::endl;
        throw std::runtime_error("Error opening destination file");
    }

    dest << src.rdbuf();

    if (!src) {
        std::cerr << "Error reading from source file: " << source << std::endl;
        throw std::runtime_error("Error reading from source file");
    }

    if (!dest) {
        std::cerr << "Error writing to destination file: " << destination << std::endl;
        throw std::runtime_error("Error writing to destination file");
    }
}

std::string Vrtbin::extractUUID() {
    std::string uuid;
    std::ifstream jsonFile(FilesystemCache::getCachePath() / "version.json");
    if (!jsonFile.is_open()) {
        uuid = "";
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
    jsonFile.close();
    return uuid;
}

void Vrtbin::progressHandler(enum ami_event_status status, uint64_t ctr, void* data) {
    struct ami_pdi_progress* prog = NULL;

    if (!data) return;

    prog = (struct ami_pdi_progress*)data;

    if (status == AMI_EVENT_STATUS_OK) prog->bytes_written += ctr;

    prog->reserved = print_progress_bar(prog->bytes_written, prog->bytes_to_write,
                                        100,  // progress bar width
                                        '[', ']', '#', '.', prog->reserved);
}

char Vrtbin::print_progress_bar(uint32_t cur, uint32_t max, uint32_t width, char left, char right,
                                char fill, char empty, char state) {
    int i = 0;
    char new_state = 0;
    uint32_t progress = 0;

    if (max == 0) max = 1;

    progress = ((unsigned long long)cur * width) / max;

    /* Move to beginning of the line */
    putchar('\r');

    /* Print left margin */
    putchar(left);

    if (width < progress) progress = width;

    for (i = 0; i < progress; i++) putchar(fill);

    for (i = 0; i < (width - progress); i++) putchar(empty);

    putchar(right);
    printf(" %.0f%% ", ((double)cur / (double)max) * 100);

    switch (state) {
        case '|':
            putchar('|');
            new_state = '-';
            break;

        case '-':
            putchar('-');
            new_state = '|';
            break;

        default:
            putchar('|');
            new_state = '-';
            break;
    }

    /* Print space so cursor doesn't obstruct last character. */
    putchar(' ');

    fflush(stdout);
    return new_state;
}

void Vrtbin::extractAndPrintInfo(const std::string& path) {
    std::ifstream jsonFile(path);
    if (!jsonFile.is_open()) {
        std::cerr << "Error: could not open file " << path << std::endl;
        return;
    }

    std::string line;
    char name[256] = {0};
    char release[256] = {0};
    char logicUuid[256] = {0};
    char application[256] = {0};

    while (std::getline(jsonFile, line)) {
        if (line.find("\"name\":") != std::string::npos) {
            sscanf(line.c_str(), " \"name\": \"%[^\"]\"", name);
        } else if (line.find("\"release\":") != std::string::npos) {
            sscanf(line.c_str(), " \"release\": \"%[^\"]\"", release);
        } else if (line.find("\"logic_uuid\":") != std::string::npos) {
            sscanf(line.c_str(), " \"logic_uuid\": \"%[^\"]\"", logicUuid);
        } else if (line.find("\"application\":") != std::string::npos) {
            sscanf(line.c_str(), " \"application\": \"%[^\"]\"", application);
        }
    }

    jsonFile.close();

    std::cout << "--------------------------------------------------------------------\n";
    std::cout << "Design Information\n";
    std::cout << "--------------------------------------------------------------------\n";
    std::cout << "Design Name                 | " << name << "\n";
    std::cout << "Release                     | " << release << "\n";
    std::cout << "Logic UUID                  | " << logicUuid << "\n";
    std::cout << "Application                 | " << application << "\n\n";
}
