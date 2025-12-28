/**
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef LIBSLASH_QDMA_H
#define LIBSLASH_QDMA_H

#include "uapi/slash_interface.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct slash_qdma {
    int fd;
    bool mock;
};

struct slash_qdma *slash_qdma_open(const char *path);
int slash_qdma_close(struct slash_qdma *qdma);

int slash_qdma_info_read(struct slash_qdma *qdma, struct slash_qdma_info *info);

int slash_qdma_qpair_add(struct slash_qdma *qdma,
                         struct slash_qdma_qpair_add *req);

int slash_qdma_qpair_start(struct slash_qdma *qdma, uint32_t qid);
int slash_qdma_qpair_stop(struct slash_qdma *qdma, uint32_t qid);
int slash_qdma_qpair_del(struct slash_qdma *qdma, uint32_t qid);

int slash_qdma_qpair_get_fd(struct slash_qdma *qdma, uint32_t qid, int flags);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LIBSLASH_QDMA_H */

