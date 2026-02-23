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

#include "parser/xml_parser.hpp"

namespace vrt {
XMLParser::XMLParser(const std::string& file_path) {
    this->filename = file_path;
    this->document = xmlReadFile(this->filename.c_str(), NULL, 0);
    this->rootNode = xmlDocGetRootElement(this->document);
    this->workingNode = rootNode->children;
}

void XMLParser::parseXML() {
    for (xmlNode* kernelNode = rootNode->children; kernelNode; kernelNode = kernelNode->next) {
        if (kernelNode->type == XML_ELEMENT_NODE &&
            xmlStrcmp(kernelNode->name, BAD_CAST "Kernel") == 0) {
            std::string name;
            std::string baseAddress;
            std::string range;
            std::vector<Register> registers;
            for (xmlNode* childNode = kernelNode->children; childNode;
                 childNode = childNode->next) {
                if (childNode->type == XML_ELEMENT_NODE) {
                    if (xmlStrcmp(childNode->name, BAD_CAST "Name") == 0) {
                        name = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "BaseAddress") == 0) {
                        baseAddress = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "Range") == 0) {
                        range = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "register") == 0) {
                        std::string offset = (const char*)xmlGetProp(childNode, BAD_CAST "offset");
                        std::string regName = (const char*)xmlGetProp(childNode, BAD_CAST "name");
                        std::string access = (const char*)xmlGetProp(childNode, BAD_CAST "access");
                        std::string description =
                            (const char*)xmlGetProp(childNode, BAD_CAST "description");
                        std::string regRange = (const char*)xmlGetProp(childNode, BAD_CAST "range");
                        Register reg;
                        reg.setOffset(std::stoi(offset, nullptr, 16));
                        reg.setRegisterName(regName);
                        reg.setRW(access);
                        reg.setDescription(description);
                        reg.setWidth(std::stoi(regRange));
                        registers.push_back(reg);
                    }
                }
            }
            auto ba = std::stoull(baseAddress, nullptr, 16);
            auto r = std::stoull(range, nullptr, 16);
            Kernel kernel(name, ba, r, registers);
            kernels[name] = kernel;
        } else if (kernelNode->type == XML_ELEMENT_NODE &&
                   xmlStrcmp(kernelNode->name, BAD_CAST "ClockFrequency") == 0) {
            std::string clkFreq = (const char*)xmlNodeGetContent(kernelNode);
            this->clockFrequency = std::stoull(clkFreq);
        } else if (kernelNode->type == XML_ELEMENT_NODE &&
                   xmlStrcmp(kernelNode->name, BAD_CAST "Type") == 0) {
            std::string type = (const char*)xmlNodeGetContent(kernelNode);
            this->vrtbinType = (type == "Full") ? VrtbinType::FLAT : VrtbinType::SEGMENTED;
        } else if (kernelNode->type == XML_ELEMENT_NODE &&
                   xmlStrcmp(kernelNode->name, BAD_CAST "Platform") == 0) {
            std::string platform_ = (const char*)xmlNodeGetContent(kernelNode);
            this->platform = (platform_ == "Hardware")     ? Platform::HARDWARE
                             : (platform_ == "Emulation")  ? Platform::EMULATION
                             : (platform_ == "Simulation") ? Platform::SIMULATION
                                                           : Platform::UNKNOWN;
            if (this->platform == Platform::UNKNOWN) {
                throw std::runtime_error("Unknown platform type");
            }
        } else if (kernelNode->type == XML_ELEMENT_NODE &&
                   xmlStrcmp(kernelNode->name, BAD_CAST "Qdma") == 0) {
            std::string kernelName, qdmaStream, syncTypeStr;
            uint32_t qid;
            for (xmlNode* childNode = kernelNode->children; childNode;
                 childNode = childNode->next) {
                if (childNode->type == XML_ELEMENT_NODE) {
                    if (xmlStrcmp(childNode->name, BAD_CAST "kernel") == 0) {
                        kernelName = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "interface") == 0) {
                        qdmaStream = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "direction") == 0) {
                        syncTypeStr = (const char*)xmlNodeGetContent(childNode);
                    } else if (xmlStrcmp(childNode->name, BAD_CAST "qid") == 0) {
                        qid = std::stoi(std::string((const char*)xmlNodeGetContent(childNode)));
                    }
                }
            }
            qdmaConnections.push_back({kernelName, qid, qdmaStream, syncTypeStr});
        }
    }
}

std::map<std::string, Kernel> XMLParser::getKernels() { return kernels; }

uint64_t XMLParser::getClockFrequency() { return this->clockFrequency; }

VrtbinType XMLParser::getVrtbinType() { return this->vrtbinType; }

Platform XMLParser::getPlatform() { return this->platform; }

std::vector<QdmaConnection> XMLParser::getQdmaConnections() { return this->qdmaConnections; }

XMLParser::~XMLParser() {
    if (this->document != nullptr) {
        xmlFreeDoc(this->document);
    }
    xmlCleanupParser();
}

}  // namespace vrt
