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

#ifndef VRTD_ALLOCATOR_H
#define VRTD_ALLOCATOR_H

#include <stdint.h>
#include <stddef.h>

// Regions are 512MB
// Subregions are 64MB
#define HBM_REGIONS 64
#define DDR_REGIONS 64
#define HBM_START_ADDRESS 0x4000000000ULL
#define DDR_START_ADDRESS 0x60000000000ULL
#define REGION_SIZE (512UL * 1024 * 1024)
#define SUBREGION_SIZE (64UL * 1024 * 1024)
#define SUBREGIONS_PER_REGION (REGION_SIZE / SUBREGION_SIZE)

struct ddr_region_data {
    uint64_t client_id[SUBREGIONS_PER_REGION]; // 0 if free, non-zero owner connection id if allocated
};

struct hbm_region_data {
    uint64_t client_id[SUBREGIONS_PER_REGION]; // 0 if free, non-zero owner connection id if allocated
};

struct device_memory_map {
    struct ddr_region_data ddr_regions[DDR_REGIONS];
    struct hbm_region_data hbm_regions[HBM_REGIONS];
};

enum allocation_type {
    ALLOCATION_TYPE_DDR,
    ALLOCATION_TYPE_HBM,
    ALLOCATION_TYPE_HBM_VNOC,
};

struct device_memory_map *device_memory_map_create(void);
void device_memory_map_cleanup(struct device_memory_map *map);
static inline
void device_memory_map_cleanupp(struct device_memory_map **mapp)
{
    device_memory_map_cleanup(*mapp);
    *mapp = NULL;
}

enum allocation_result {
    ALLOCATION_RESULT_SUCCESS = 0,
    ALLOCATION_RESULT_NO_MEMORY = 1,
    ALLOCATION_RESULT_BAD_ARGUMENT = 2,
};

// For ALLOCATION_TYPE_HBM (non-VNOC), arg specifies the HBM region index (0-63).
// Otherwise, arg is ignored.
// size is input as the requested size, and output as the allocated size (rounded up to
// the nearest subregion).
enum allocation_result device_memory_map_allocate(struct device_memory_map *map,
                                                  enum allocation_type type,
                                                  uint64_t *size,
                                                  uint64_t arg,
                                                  uint64_t client_id,
                                                  uint64_t *addr_out);

enum allocation_result device_memory_map_free(struct device_memory_map *map,
                                              enum allocation_type type,
                                              uint64_t addr,
                                              uint64_t size,
                                              uint64_t client_id);

#endif // VRTD_ALLOCATOR_H
