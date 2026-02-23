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

#ifndef VRTD_BUFFER_H
#define VRTD_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <slash/qdma.h>

#include "allocator.h"
#include "array.h"
#include "vrtd/wire.h"

struct buffer {
    struct slash_qdma *qdma; /* non-owning */
    struct device_memory_map *map; /* non-owning */
    enum allocation_type alloc_type;
    uint64_t alloc_arg;
    enum vrtd_alloc_dir alloc_dir;
    uint64_t client_id; /* owning connection id */
    uint64_t addr;
    uint64_t size;
    uint32_t qid;
    int fd;
    bool allocation_valid;
    bool qpair_created;
};

struct buffer *buffer_create(struct slash_qdma *qdma,
                             struct device_memory_map *map,
                             enum allocation_type alloc_type,
                             enum vrtd_alloc_dir alloc_dir,
                             uint64_t size,
                             uint64_t alloc_arg,
                             uint64_t client_id,
                             const struct slash_qdma_qpair_add *qpair_params);
void cleanup_buffer(struct buffer *buf);
static inline
void cleanup_bufferp(struct buffer **bufp)
{
    cleanup_buffer(*bufp);
    *bufp = NULL;
}

DECLARE_OWNING_PTR_ARRAY(buffer_ptr_array, struct buffer *, cleanup_buffer);

#endif // VRTD_BUFFER_H
