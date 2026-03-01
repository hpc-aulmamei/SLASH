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

#ifndef VRTD_DESIGN_WRITER_H
#define VRTD_DESIGN_WRITER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <slash/qdma.h>

struct design_writer {
    struct slash_qdma *qdma; /* non-owning */
    uint32_t qid;
    int fd;
    bool qpair_created;
    bool qpair_started;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int input_fd;
    bool busy;
    bool stop;
    int last_error;
    bool thread_started;
    bool mutex_initialized;
    bool cond_initialized;
};

struct design_writer *design_writer_create(struct slash_qdma *qdma);
int design_writer_submit_fd_async(struct design_writer *writer, int fd);
int design_writer_submit_fd(struct design_writer *writer, int fd);
int design_writer_poll_result(struct design_writer *writer, bool *done, int *last_error);
bool design_writer_is_busy(struct design_writer *writer);
void cleanup_design_writer(struct design_writer *writer);
static inline
void cleanup_design_writerp(struct design_writer **writerp)
{
    cleanup_design_writer(*writerp);
    *writerp = NULL;
}

#endif // VRTD_DESIGN_WRITER_H
