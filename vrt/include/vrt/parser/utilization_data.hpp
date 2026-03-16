/**
 * The MIT License (MIT)
 * Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
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

#ifndef VRT_UTILIZATION_DATA_HPP
#define VRT_UTILIZATION_DATA_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vrt {

struct ResourceMetrics {
    uint32_t totalPplocs = 0;
    uint32_t totalLuts = 0;
    uint32_t lutram = 0;
    uint32_t srl = 0;
    uint32_t ff = 0;
    uint32_t ramb36 = 0;
    uint32_t ramb18 = 0;
    uint32_t ramb = 0;
    uint32_t uram = 0;
    uint32_t dsp = 0;

    std::optional<float> totalLutsPct;
    std::optional<float> lutramPct;
    std::optional<float> srlPct;
    std::optional<float> ffPct;
    std::optional<float> ramb36Pct;
    std::optional<float> ramb18Pct;
    std::optional<float> uramPct;
    std::optional<float> dspPct;
};

struct UtilizationCell {
    std::string instance;
    std::string module;
    std::string pr;
    ResourceMetrics metrics;
};

struct Subhierarchy {
    std::vector<UtilizationCell> cells;
    std::vector<UtilizationCell> slashLogic;
    ResourceMetrics subhierarchySum;
    ResourceMetrics slashLogicSum;
};

struct UtilizationBlock {
    std::string name;
    std::string instance;
    std::string pr;
    ResourceMetrics totals;
    std::optional<Subhierarchy> subhierarchy;
};

struct UtilizationReport {
    UtilizationBlock slash;
    std::optional<UtilizationBlock> serviceLayer;
};

}  // namespace vrt

#endif  // VRT_UTILIZATION_DATA_HPP
