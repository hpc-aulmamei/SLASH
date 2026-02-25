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

#define _GNU_SOURCE

#include "allocator.h"

#include <stdlib.h>

struct device_memory_map *device_memory_map_create(void)
{
    return calloc(1, sizeof(struct device_memory_map));
}

void device_memory_map_cleanup(struct device_memory_map *map)
{
    if (map == NULL) {
        return;
    }

    free(map);
}

enum allocation_result device_memory_map_allocate(struct device_memory_map *map,
                                                  enum allocation_type type,
                                                  uint64_t *size,
                                                  uint64_t arg,
                                                  uint64_t client_id,
                                                  uint64_t *addr_out)

{
    if (map == NULL || size == NULL || addr_out == NULL || *size == 0) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    uint64_t num_subregions = (*size + SUBREGION_SIZE - 1) / SUBREGION_SIZE;

    if (client_id == 0) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    switch (type) {
    case ALLOCATION_TYPE_DDR: {
        if (num_subregions > SUBREGIONS_PER_REGION) {
            return ALLOCATION_RESULT_BAD_ARGUMENT;
        }
        // Find a contiguous set of free subregions in DDR
        for (size_t region_idx = 0; region_idx < DDR_REGIONS; region_idx++) {
            size_t contiguous_free = 0;
            for (size_t subregion_idx = 0; subregion_idx < SUBREGIONS_PER_REGION; subregion_idx++) {
                if (map->ddr_regions[region_idx].client_id[subregion_idx] == 0) {
                    contiguous_free++;
                    if (contiguous_free == num_subregions) {
                        // Found a suitable block
                        size_t start_subregion = subregion_idx + 1 - num_subregions;
                        for (size_t i = 0; i < num_subregions; i++) {
                            map->ddr_regions[region_idx].client_id[start_subregion + i] = client_id;
                        }
                        *addr_out = DDR_START_ADDRESS + (region_idx * REGION_SIZE) +
                                    (start_subregion * SUBREGION_SIZE);
                        *size = num_subregions * SUBREGION_SIZE;
                        return ALLOCATION_RESULT_SUCCESS;
                    }
                } else {
                    contiguous_free = 0;
                }
            }
        }
        // No suitable block found
        return ALLOCATION_RESULT_NO_MEMORY;
    }

    case ALLOCATION_TYPE_HBM: {
        if (arg >= HBM_REGIONS || num_subregions > SUBREGIONS_PER_REGION) {
            return ALLOCATION_RESULT_BAD_ARGUMENT;
        }

        size_t region_idx = (size_t)arg;

        if (region_idx >= HBM_REGIONS) {
            return ALLOCATION_RESULT_BAD_ARGUMENT;
        }

        size_t contiguous_free = 0;
        for (size_t subregion_idx = 0; subregion_idx < SUBREGIONS_PER_REGION; subregion_idx++) {
            if (map->hbm_regions[region_idx].client_id[subregion_idx] == 0) {
                contiguous_free++;
                if (contiguous_free == num_subregions) {
                    size_t start_subregion = subregion_idx + 1 - num_subregions;
                    for (size_t i = 0; i < num_subregions; i++) {
                        map->hbm_regions[region_idx].client_id[start_subregion + i] = client_id;
                    }
                    *addr_out = HBM_START_ADDRESS + (region_idx * REGION_SIZE) +
                                (start_subregion * SUBREGION_SIZE);
                    *size = num_subregions * SUBREGION_SIZE;
                    return ALLOCATION_RESULT_SUCCESS;
                }
            } else {
                contiguous_free = 0;
            }
        }

        return ALLOCATION_RESULT_NO_MEMORY;
    }

    case ALLOCATION_TYPE_HBM_VNOC: {
        if (num_subregions > SUBREGIONS_PER_REGION) {
            return ALLOCATION_RESULT_BAD_ARGUMENT;
        }

        // Find a contiguous set of free subregions in HBM across any region
        for (size_t region_idx = 0; region_idx < HBM_REGIONS; region_idx++) {
            size_t contiguous_free = 0;
            for (size_t subregion_idx = 0; subregion_idx < SUBREGIONS_PER_REGION; subregion_idx++) {
                if (map->hbm_regions[region_idx].client_id[subregion_idx] == 0) {
                    contiguous_free++;
                    if (contiguous_free == num_subregions) {
                        size_t start_subregion = subregion_idx + 1 - num_subregions;
                        for (size_t i = 0; i < num_subregions; i++) {
                            map->hbm_regions[region_idx].client_id[start_subregion + i] = client_id;
                        }
                        *addr_out = HBM_START_ADDRESS + (region_idx * REGION_SIZE) +
                                    (start_subregion * SUBREGION_SIZE);
                        *size = num_subregions * SUBREGION_SIZE;
                        return ALLOCATION_RESULT_SUCCESS;
                    }
                } else {
                    contiguous_free = 0;
                }
            }
        }

        return ALLOCATION_RESULT_NO_MEMORY;
    }

    }

    return ALLOCATION_RESULT_BAD_ARGUMENT;
}

enum allocation_result device_memory_map_free(struct device_memory_map *map,
                                              enum allocation_type type,
                                              uint64_t addr,
                                              uint64_t size,
                                              uint64_t client_id)
{
    if (map == NULL || size == 0 || client_id == 0) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    uint64_t base;
    uint64_t max_size;
    struct ddr_region_data *ddr_regions = NULL;
    struct hbm_region_data *hbm_regions = NULL;

    switch (type) {
    case ALLOCATION_TYPE_DDR:
        base = DDR_START_ADDRESS;
        max_size = DDR_REGIONS * REGION_SIZE;
        ddr_regions = map->ddr_regions;
        break;
    case ALLOCATION_TYPE_HBM:
    case ALLOCATION_TYPE_HBM_VNOC:
        base = HBM_START_ADDRESS;
        max_size = HBM_REGIONS * REGION_SIZE;
        hbm_regions = map->hbm_regions;
        break;
    default:
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    if (addr < base || addr >= base + max_size) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    uint64_t offset = addr - base;
    if ((offset % SUBREGION_SIZE) != 0) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    uint64_t num_subregions = (size + SUBREGION_SIZE - 1) / SUBREGION_SIZE;
    size_t region_idx = (size_t)(offset / REGION_SIZE);
    size_t start_subregion = (size_t)((offset % REGION_SIZE) / SUBREGION_SIZE);

    if (num_subregions > SUBREGIONS_PER_REGION ||
        start_subregion + num_subregions > SUBREGIONS_PER_REGION) {
        return ALLOCATION_RESULT_BAD_ARGUMENT;
    }

    if (ddr_regions != NULL) {
        for (size_t i = 0; i < num_subregions; i++) {
            if (ddr_regions[region_idx].client_id[start_subregion + i] != client_id) {
                return ALLOCATION_RESULT_BAD_ARGUMENT;
            }
        }
        for (size_t i = 0; i < num_subregions; i++) {
            ddr_regions[region_idx].client_id[start_subregion + i] = 0;
        }
        return ALLOCATION_RESULT_SUCCESS;
    }

    for (size_t i = 0; i < num_subregions; i++) {
        if (hbm_regions[region_idx].client_id[start_subregion + i] != client_id) {
            return ALLOCATION_RESULT_BAD_ARGUMENT;
        }
    }
    for (size_t i = 0; i < num_subregions; i++) {
        hbm_regions[region_idx].client_id[start_subregion + i] = 0;
    }
    return ALLOCATION_RESULT_SUCCESS;
}
