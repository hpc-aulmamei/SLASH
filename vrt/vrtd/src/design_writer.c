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

#include "design_writer.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <unistd.h>

#include <systemd/sd-journal.h>
#include <syslog.h>


#define VRTD_QDMA_Q_MODE_MM 0u
#define VRTD_QDMA_DIR_H2C (1u << 0)
#define VRTD_QDMA_DIR_C2H (1u << 1)
#define VRTD_QDMA_RING_SZ_IDX 9u
#define VRTD_DESIGN_WRITER_SEEK_ADDR 0x102100000ull
#define VRTD_DESIGN_WRITER_MAX_BYTES (1ull * 1024 * 1024 * 1024) // 1 GiB
#define VRTD_DESIGN_WRITER_CHUNK_BYTES 4096u

#define READ_ENTIRE_FILE_ALLOCATION_STEP (2 * 1024 * 1024) // 2 MiB

static int design_writer_open_qpair(struct design_writer *writer);
static void design_writer_release_qpair(struct design_writer *writer);

static int realloc_alligned_memory(void **bufp, size_t old_size, size_t new_size)
{
    void *new_buf;
    int ret = posix_memalign(&new_buf, 4096, new_size);
    if (ret != 0) {
        PROPAGATE_ERROR_LOG(-1, LOG_ERR, "Failed to allocate memory: %s", strerrordesc_np(ret));
    }

    memcpy(new_buf, *bufp, old_size);

    free(*bufp);
    *bufp = new_buf;
    return 0;
}

static ssize_t read_entire_file(int fd, void **bufp)
{
    size_t capacity = READ_ENTIRE_FILE_ALLOCATION_STEP;
    size_t size = 0;
    
    _cleanup_(cleanup_free)
    uint8_t *buf;
    int ret = posix_memalign((void **)&buf, 4096, capacity);
    if (ret != 0) {
        PROPAGATE_ERROR_LOG(-1, LOG_ERR, "Failed to allocate memory for file buffer: %s", strerrordesc_np(ret));
    }

    for (;;) {
        ssize_t n = read(fd, buf + size, capacity - size);
        if (n == 0) { // EOF
            break;
        }
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            PROPAGATE_ERROR_STDC_LOG(-1, LOG_ERR, "Failed to read file");
        }
        size += (size_t)n;

        if (size == capacity) {
            capacity += READ_ENTIRE_FILE_ALLOCATION_STEP;
            ret = realloc_alligned_memory((void **)&buf, size, capacity);
            PROPAGATE_ERROR(ret);
        }

        if (capacity > VRTD_DESIGN_WRITER_MAX_BYTES) {
            PROPAGATE_ERROR_LOG(-1, LOG_ERR, "File size exceeds design writer maximum supported size of %zu bytes", (size_t)VRTD_DESIGN_WRITER_MAX_BYTES);
        }
    }

    *bufp = buf;
    buf = NULL; // ownership transferred to caller

    return (ssize_t)size;
}

static int write_all_at_pos(int fd, const void *buf, size_t len, off_t pos)
{
    size_t off = 0;

    while (off < len) {
        LOG(
            LOG_INFO,
            "Attempting to write to design writer file descriptor at offset 0x%lx (progress: %zu/%zu)",
            (unsigned long)(pos + off), off, len
        );

        off_t ret = lseek(fd, pos + off, SEEK_SET);
        PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Failed to seek design writer file descriptor to position 0x%lx", (unsigned long)(pos + off));

        ssize_t n = write(fd, (const uint8_t *)buf + off, len - off);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            PROPAGATE_ERROR_STDC_LOG(-1, LOG_ERR, "Failed to write to design writer file descriptor");
        }
        if (n == 0) {
            errno = EIO;
            PROPAGATE_ERROR_STDC_LOG(-1, LOG_ERR, "Short write to design writer file descriptor");
        }

        off += (size_t)n;

        LOG(
            LOG_INFO,
            "Design writer: wrote %zu bytes at offset 0x%lx (total written: %zu/%zu)",
            (size_t)n, (unsigned long)(pos + off), off, len
        );
    }

    return 0;
}

static int design_writer_transfer(struct design_writer *writer, int input_fd)
{
    _cleanup_(cleanup_free)
    void *file_data = NULL;
    ssize_t bytes_read = read_entire_file(input_fd, &file_data);
    PROPAGATE_ERROR_LOG(bytes_read, LOG_ERR, "Failed to read entire input file for design writer transfer");

    // int ret = design_writer_open_qpair(writer);
    // PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize design writer qpair");

    int ret = write_all_at_pos(writer->fd, file_data, (size_t)bytes_read, VRTD_DESIGN_WRITER_SEEK_ADDR);
    int saved_errno = errno;
    // design_writer_release_qpair(writer);
    // errno = saved_errno;
    // PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to transfer design writer payload");

    return 0;
}

static void *design_writer_thread(void *arg)
{
    struct design_writer *writer = arg;

    (void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    (void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    for (;;) {
        (void) pthread_mutex_lock(&writer->mutex);
        while (!writer->stop && writer->input_fd < 0) {
            (void) pthread_cond_wait(&writer->cond, &writer->mutex);
        }
        if (writer->stop) {
            (void) pthread_mutex_unlock(&writer->mutex);
            break;
        }

        int input_fd = writer->input_fd;
        (void) pthread_mutex_unlock(&writer->mutex);

        int transfer_errno = 0;

        (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        LOG(LOG_INFO, "Design writer transfer starting");
        if (input_fd >= 0) {
            if (design_writer_transfer(writer, input_fd) != 0) {
                transfer_errno = (errno != 0) ? errno : EIO;
                LOG(
                    LOG_WARNING,
                    "Design writer transfer failed: %m"
                );
            }
            (void) close(input_fd);
        }
        (void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        (void) pthread_mutex_lock(&writer->mutex);
        writer->input_fd = -1;
        writer->busy = false;
        writer->last_error = transfer_errno;
        (void) pthread_cond_broadcast(&writer->cond);
        (void) pthread_mutex_unlock(&writer->mutex);
    }

    return NULL;
}

static void cleanup_close_fd(int *fdp)
{
    if (fdp == NULL || *fdp < 0) {
        return;
    }

    (void) close(*fdp);
    *fdp = -1;
}

static void cleanup_mutex_unlockp(pthread_mutex_t **mutexp)
{
    if (mutexp == NULL || *mutexp == NULL) {
        return;
    }

    (void) pthread_mutex_unlock(*mutexp);
    *mutexp = NULL;
}

static void design_writer_release_qpair(struct design_writer *writer)
{
    if (writer == NULL) {
        return;
    }

    cleanup_close_fd(&writer->fd);

    if (writer->qpair_created && writer->qdma != NULL) {
        if (writer->qpair_started && slash_qdma_qpair_stop(writer->qdma, writer->qid) == -1) {
            LOG(
                LOG_WARNING,
                "Error stopping design writer qpair %u: %m (ignored)",
                writer->qid
            );
        }

        if (slash_qdma_qpair_del(writer->qdma, writer->qid) == -1) {
            LOG(
                LOG_WARNING,
                "Error deleting design writer qpair %u: %m (ignored)",
                writer->qid
            );
        }
    }

    writer->qid = 0;
    writer->qpair_started = false;
    writer->qpair_created = false;
}

static void design_writer_release_resources(struct design_writer *writer)
{
    if (writer->mutex_initialized) {
        (void) pthread_mutex_lock(&writer->mutex);
        writer->stop = true;
        if (writer->cond_initialized) {
            (void) pthread_cond_broadcast(&writer->cond);
        }
        (void) pthread_mutex_unlock(&writer->mutex);
    }

    if (writer->thread_started) {
        (void) pthread_cancel(writer->thread);
        (void) pthread_join(writer->thread, NULL);
        writer->thread_started = false;
    }

    cleanup_close_fd(&writer->input_fd);
    design_writer_release_qpair(writer);

    writer->qdma = NULL;

    if (writer->cond_initialized) {
        (void) pthread_cond_destroy(&writer->cond);
        writer->cond_initialized = false;
    }
    if (writer->mutex_initialized) {
        (void) pthread_mutex_destroy(&writer->mutex);
        writer->mutex_initialized = false;
    }
}

static void cleanup_design_writer_resourcesp(struct design_writer **writerp)
{
    if (writerp == NULL || *writerp == NULL) {
        return;
    }

    design_writer_release_resources(*writerp);
}

static int design_writer_init_sync_primitives(struct design_writer *writer)
{
    int pthread_ret = pthread_mutex_init(&writer->mutex, NULL);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to initialize design writer mutex (code=%d)",
        pthread_ret
    );
    writer->mutex_initialized = true;

    pthread_ret = pthread_cond_init(&writer->cond, NULL);
    ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to initialize design writer condition variable (code=%d)",
        pthread_ret
    );
    writer->cond_initialized = true;

    return 0;
}

static int design_writer_open_qpair(struct design_writer *writer)
{
    struct slash_qdma_qpair_add qpair = {0};
    qpair.size = sizeof(qpair);
    qpair.mode = VRTD_QDMA_Q_MODE_MM;
    qpair.dir_mask = VRTD_QDMA_DIR_H2C;
    qpair.h2c_ring_sz = VRTD_QDMA_RING_SZ_IDX;
    qpair.c2h_ring_sz = VRTD_QDMA_RING_SZ_IDX;
    qpair.cmpt_ring_sz = VRTD_QDMA_RING_SZ_IDX;

    int ret = slash_qdma_qpair_add(writer->qdma, &qpair);
    PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Failed to add design writer QDMA qpair");

    writer->qid = qpair.qid;
    writer->qpair_created = true;
    writer->qpair_started = false;

    ret = slash_qdma_qpair_start(writer->qdma, writer->qid);
    if (ret == -1) {
        LOG(LOG_ERR, "Failed to start design writer QDMA qpair: %m");
        design_writer_release_qpair(writer);
        return -1;
    }
    writer->qpair_started = true;

    writer->fd = slash_qdma_qpair_get_fd(writer->qdma, writer->qid, O_CLOEXEC);
    if (writer->fd == -1) {
        LOG(LOG_ERR, "Failed to get design writer QDMA file descriptor: %m");
        design_writer_release_qpair(writer);
        return -1;
    }

    return 0;
}

static int design_writer_start_thread(struct design_writer *writer)
{
    int pthread_ret = pthread_create(&writer->thread, NULL, design_writer_thread, writer);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to create design writer thread (code=%d)",
        pthread_ret
    );

    writer->thread_started = true;
    return 0;
}

static int design_writer_init(struct design_writer *writer, struct slash_qdma *qdma)
{
    PROPAGATE_ERROR_NULL_LOG(writer, LOG_ERR, "Failed to initialize design writer: invalid writer");
    PROPAGATE_ERROR_NULL_LOG(qdma, LOG_ERR, "Failed to initialize design writer: invalid qdma");

    *writer = (struct design_writer) {
        .qdma = qdma,
        .qid = 0,
        .fd = -1,
        .qpair_created = false,
        .qpair_started = false,
        .thread = 0,
        .input_fd = -1,
        .busy = false,
        .stop = false,
        .last_error = 0,
        .thread_started = false,
        .mutex_initialized = false,
        .cond_initialized = false,
    };

    _cleanup_(cleanup_design_writer_resourcesp)
    struct design_writer *writer_rollback = writer;

    int ret = design_writer_open_qpair(writer);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize design writer qpair");

    ret = design_writer_init_sync_primitives(writer);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize design writer synchronization primitives");

    ret = design_writer_start_thread(writer);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to start design writer worker thread");

    writer_rollback = NULL;

    return 0;
}

static int design_writer_create_internal(struct slash_qdma *qdma, struct design_writer **writerp)
{
    PROPAGATE_ERROR_NULL_LOG(writerp, LOG_ERR, "Failed to create design writer: invalid output pointer");

    _cleanup_(cleanup_design_writerp)
    struct design_writer *writer = calloc(1, sizeof(*writer));
    PROPAGATE_ERROR_NULL_STDC_LOG(writer, LOG_ERR, "Failed to allocate design writer");

    int ret = design_writer_init(writer, qdma);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize design writer");

    *writerp = writer;
    writer = NULL;

    return 0;
}

struct design_writer *design_writer_create(struct slash_qdma *qdma)
{
    struct design_writer *writer = NULL;
    if (design_writer_create_internal(qdma, &writer) == -1) {
        return NULL;
    }

    return writer;
}

int design_writer_submit_fd(struct design_writer *writer, int fd)
{
    int ret = design_writer_submit_fd_async(writer, fd);
    PROPAGATE_ERROR_LOG(ret, LOG_WARNING, "Failed to enqueue design write request");

    int pthread_ret = pthread_mutex_lock(&writer->mutex);
    ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to lock design writer mutex (code=%d)",
        pthread_ret
    );
    _cleanup_(cleanup_mutex_unlockp)
    pthread_mutex_t *locked_mutex = &writer->mutex;

    while (writer->busy && !writer->stop) {
        (void) pthread_cond_wait(&writer->cond, &writer->mutex);
    }

    ret = writer->stop ? -1 : 0;
    PROPAGATE_ERROR_LOG(ret, LOG_WARNING, "Design writer stopped before transfer completed");

    int last_error = writer->last_error;
    ret = last_error == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_WARNING,
        "Design writer transfer failed (code=%d)",
        last_error
    );

    return 0;
}

int design_writer_submit_fd_async(struct design_writer *writer, int fd)
{
    PROPAGATE_ERROR_NULL_LOG(writer, LOG_ERR, "design_writer_submit_fd_async called with null writer");
    PROPAGATE_ERROR_LOG(
        (fd >= 0) ? 0 : -1,
        LOG_ERR,
        "design_writer_submit_fd_async called with invalid fd %d",
        fd
    );

    int pthread_ret = pthread_mutex_lock(&writer->mutex);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to lock design writer mutex (code=%d)",
        pthread_ret
    );
    _cleanup_(cleanup_mutex_unlockp)
    pthread_mutex_t *locked_mutex = &writer->mutex;

    ret = (writer->stop || writer->busy || writer->input_fd >= 0) ? -1 : 0;
    PROPAGATE_ERROR_LOG(ret, LOG_WARNING, "Design writer is busy or stopping");

    writer->input_fd = fd;
    writer->busy = true;
    writer->last_error = 0;
    (void) pthread_cond_signal(&writer->cond);

    LOG(LOG_DEBUG, "Design write enqueued fd=%d", fd);
    return 0;
}

int design_writer_poll_result(struct design_writer *writer, bool *done, int *last_error)
{
    PROPAGATE_ERROR_NULL_LOG(writer, LOG_ERR, "design_writer_poll_result called with null writer");
    PROPAGATE_ERROR_NULL_LOG(done, LOG_ERR, "design_writer_poll_result called with null done pointer");
    PROPAGATE_ERROR_NULL_LOG(last_error, LOG_ERR, "design_writer_poll_result called with null last_error pointer");

    int pthread_ret = pthread_mutex_lock(&writer->mutex);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(
        ret,
        LOG_ERR,
        "Failed to lock design writer mutex (code=%d)",
        pthread_ret
    );
    _cleanup_(cleanup_mutex_unlockp)
    pthread_mutex_t *locked_mutex = &writer->mutex;

    *done = !writer->busy;
    if (writer->stop) {
        *done = true;
        *last_error = ECANCELED;
    } else {
        *last_error = writer->last_error;
    }

    return 0;
}

bool design_writer_is_busy(struct design_writer *writer)
{
    if (writer == NULL) {
        return false;
    }

    (void) pthread_mutex_lock(&writer->mutex);
    bool busy = writer->busy;
    (void) pthread_mutex_unlock(&writer->mutex);
    return busy;
}

void cleanup_design_writer(struct design_writer *writer)
{
    if (writer == NULL) {
        return;
    }

    design_writer_release_resources(writer);
    free(writer);
}
