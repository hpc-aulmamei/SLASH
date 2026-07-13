/**
 * The MIT License (MIT)
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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

/**
 * @file flash_worker.c
 * @brief Background-thread execution of cfgmem programming for vrtd.
 *
 * Threading model
 * ---------------
 * A single dedicated worker thread runs for the lifetime of the flash_worker.
 * The event-loop thread submits one job at a time via
 * flash_worker_submit_async(); the worker performs the long-running
 * cfgmem_program_with_ami() (AMI PDI download + SBR reset sequence) in the
 * background and publishes a VRTD_RET_* result that the event loop retrieves
 * with flash_worker_poll_result().
 *
 * The interaction follows the same single-slot producer-consumer protocol as
 * the design writer, protected by a mutex + condition variable:
 *
 *   1. Submit stores the job parameters, takes ownership of the PDI fd, sets
 *      busy, and signals the condvar.
 *   2. The worker wakes, runs the job (with cancellation enabled so teardown
 *      can interrupt a stuck program), closes the fd, then publishes the
 *      result, clears busy, and broadcasts the condvar.
 *   3. poll_result snapshots busy + result under the mutex.
 */

#define _GNU_SOURCE

#include "flash_worker.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <unistd.h>

#include <vrtd/wire.h>

#include "flash.h"
#include "utils.h"

/**
 * @brief State for the asynchronous cfgmem programming worker.
 */
struct flash_worker {
    /** @brief Background worker thread handle. */
    pthread_t thread;
    /** @brief True once @c thread has been created. */
    bool thread_started;

    /** @brief Guards all mutable state below. */
    pthread_mutex_t mutex;
    /** @brief True once @c mutex has been initialized. */
    bool mutex_initialized;
    /** @brief Signals job submission, completion, and stop. */
    pthread_cond_t cond;
    /** @brief True once @c cond has been initialized. */
    bool cond_initialized;

    /** @brief Set to request the worker thread to exit. */
    bool stop;
    /** @brief True while a job is queued or running. */
    bool busy;

    /** @brief PDI file descriptor for the pending job (owned; -1 when idle). */
    int input_fd;
    /** @brief Device to program for the pending job (non-owning). */
    struct device *device;
    /** @brief Daemon device array for the pending job (non-owning). */
    struct device_ptr_array *devices;
    /** @brief AMI boot-device selector for the pending job. */
    uint8_t boot_device;
    /** @brief Flash partition for the pending job. */
    uint32_t partition;

    /** @brief VRTD_RET_* result of the most recently completed job. */
    uint16_t result;
};

/**
 * Cleanup helper for _cleanup_ attribute: unlock a mutex pointer-to-pointer.
 */
static void cleanup_mutex_unlockp(pthread_mutex_t **mutexp)
{
    if (mutexp == NULL || *mutexp == NULL) {
        return;
    }

    (void) pthread_mutex_unlock(*mutexp);
    *mutexp = NULL;
}

/**
 * Cleanup helper: close a file descriptor and reset it to -1.
 */
static void cleanup_close_fd(int *fdp)
{
    if (fdp == NULL || *fdp < 0) {
        return;
    }

    (void) close(*fdp);
    *fdp = -1;
}

/**
 * Worker thread entry point.
 *
 * Runs for the entire lifetime of the flash_worker following a
 * producer-consumer loop: wait for a job (or stop), run it, publish the
 * result.  Cancellation is disabled while touching shared state and enabled
 * only during the actual (long) program so cleanup_flash_worker() can
 * interrupt a stuck job during teardown.
 */
static void *flash_worker_thread(void *arg)
{
    struct flash_worker *worker = arg;

    (void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    (void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    for (;;) {
        (void) pthread_mutex_lock(&worker->mutex);
        while (!worker->stop && worker->input_fd < 0) {
            (void) pthread_cond_wait(&worker->cond, &worker->mutex);
        }
        if (worker->stop) {
            (void) pthread_mutex_unlock(&worker->mutex);
            break;
        }

        int input_fd = worker->input_fd;
        struct device *device = worker->device;
        struct device_ptr_array *devices = worker->devices;
        uint8_t boot_device = worker->boot_device;
        uint32_t partition = worker->partition;
        (void) pthread_mutex_unlock(&worker->mutex);

        uint16_t result = VRTD_RET_INTERNAL_ERROR;

        (void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        LOG(LOG_INFO, "Cfgmem program starting on worker thread");
        if (input_fd >= 0) {
            result = cfgmem_program_with_ami(device, devices, input_fd, boot_device, partition);
            (void) close(input_fd);
        }
        (void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        (void) pthread_mutex_lock(&worker->mutex);
        worker->input_fd = -1;
        worker->device = NULL;
        worker->devices = NULL;
        worker->busy = false;
        worker->result = result;
        (void) pthread_cond_broadcast(&worker->cond);
        (void) pthread_mutex_unlock(&worker->mutex);
    }

    return NULL;
}

/**
 * Initialize the mutex and condition variable.
 */
static int flash_worker_init_sync_primitives(struct flash_worker *worker)
{
    int pthread_ret = pthread_mutex_init(&worker->mutex, NULL);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize flash worker mutex (code=%d)", pthread_ret);
    worker->mutex_initialized = true;

    pthread_ret = pthread_cond_init(&worker->cond, NULL);
    ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize flash worker condition variable (code=%d)", pthread_ret);
    worker->cond_initialized = true;

    return 0;
}

/**
 * Tear down all resources owned by the worker: stop + join the thread, close
 * any pending fd, and destroy the sync primitives.
 */
static void flash_worker_release_resources(struct flash_worker *worker)
{
    if (worker->mutex_initialized) {
        (void) pthread_mutex_lock(&worker->mutex);
        worker->stop = true;
        if (worker->cond_initialized) {
            (void) pthread_cond_broadcast(&worker->cond);
        }
        (void) pthread_mutex_unlock(&worker->mutex);
    }

    if (worker->thread_started) {
        (void) pthread_cancel(worker->thread);
        (void) pthread_join(worker->thread, NULL);
        worker->thread_started = false;
    }

    cleanup_close_fd(&worker->input_fd);

    if (worker->cond_initialized) {
        (void) pthread_cond_destroy(&worker->cond);
        worker->cond_initialized = false;
    }
    if (worker->mutex_initialized) {
        (void) pthread_mutex_destroy(&worker->mutex);
        worker->mutex_initialized = false;
    }
}

/**
 * Cleanup helper for _cleanup_ attribute: release flash worker resources.
 */
static void cleanup_flash_worker_resourcesp(struct flash_worker **workerp)
{
    if (workerp == NULL || *workerp == NULL) {
        return;
    }

    flash_worker_release_resources(*workerp);
}

/**
 * Initialize a flash_worker: sync primitives + background thread.
 */
static int flash_worker_init(struct flash_worker *worker)
{
    PROPAGATE_ERROR_NULL_LOG(worker, LOG_ERR, "Failed to initialize flash worker: invalid worker");

    *worker = (struct flash_worker) {
        .thread_started = false,
        .stop = false,
        .busy = false,
        .input_fd = -1,
        .device = NULL,
        .devices = NULL,
        .boot_device = 0,
        .partition = 0,
        .result = VRTD_RET_INTERNAL_ERROR,
        .mutex_initialized = false,
        .cond_initialized = false,
    };

    /* Rollback guard: release everything initialized so far on any failure. */
    _cleanup_(cleanup_flash_worker_resourcesp)
    struct flash_worker *worker_rollback = worker;

    int ret = flash_worker_init_sync_primitives(worker);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to initialize flash worker synchronization primitives");

    int pthread_ret = pthread_create(&worker->thread, NULL, flash_worker_thread, worker);
    ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to create flash worker thread (code=%d)", pthread_ret);
    worker->thread_started = true;

    /* Initialization succeeded -- disarm the rollback guard. */
    worker_rollback = NULL;

    return 0;
}

struct flash_worker *flash_worker_create(void)
{
    _cleanup_(cleanup_flash_workerp)
    struct flash_worker *worker = calloc(1, sizeof(*worker));
    if (worker == NULL) {
        LOG(LOG_ERR, "Failed to allocate flash worker: %m");
        return NULL;
    }

    int ret = flash_worker_init(worker);
    if (ret != 0) {
        return NULL;
    }

    struct flash_worker *result = worker;
    worker = NULL; /* ownership transferred to caller */
    return result;
}

int flash_worker_submit_async(
    struct flash_worker *worker,
    struct device *device,
    struct device_ptr_array *devices,
    int input_fd,
    uint8_t boot_device,
    uint32_t partition
)
{
    PROPAGATE_ERROR_NULL_LOG(worker, LOG_ERR, "flash_worker_submit_async called with null worker");
    PROPAGATE_ERROR_NULL_LOG(device, LOG_ERR, "flash_worker_submit_async called with null device");
    PROPAGATE_ERROR_NULL_LOG(devices, LOG_ERR, "flash_worker_submit_async called with null devices");
    PROPAGATE_ERROR_LOG(
        (input_fd >= 0) ? 0 : -1,
        LOG_ERR,
        "flash_worker_submit_async called with invalid fd %d",
        input_fd
    );

    int pthread_ret = pthread_mutex_lock(&worker->mutex);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to lock flash worker mutex (code=%d)", pthread_ret);
    _cleanup_(cleanup_mutex_unlockp)
    pthread_mutex_t *locked_mutex = &worker->mutex;

    /* Reject if busy, stopping, or already holding a pending job. */
    ret = (worker->stop || worker->busy || worker->input_fd >= 0) ? -1 : 0;
    PROPAGATE_ERROR_LOG(ret, LOG_WARNING, "Flash worker is busy or stopping");

    worker->input_fd = input_fd;
    worker->device = device;
    worker->devices = devices;
    worker->boot_device = boot_device;
    worker->partition = partition;
    worker->busy = true;
    worker->result = VRTD_RET_INTERNAL_ERROR;
    (void) pthread_cond_signal(&worker->cond);

    return 0;
}

int flash_worker_poll_result(struct flash_worker *worker, bool *done, uint16_t *result)
{
    PROPAGATE_ERROR_NULL_LOG(worker, LOG_ERR, "flash_worker_poll_result called with null worker");
    PROPAGATE_ERROR_NULL_LOG(done, LOG_ERR, "flash_worker_poll_result called with null done pointer");
    PROPAGATE_ERROR_NULL_LOG(result, LOG_ERR, "flash_worker_poll_result called with null result pointer");

    int pthread_ret = pthread_mutex_lock(&worker->mutex);
    int ret = pthread_ret == 0 ? 0 : -1;
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to lock flash worker mutex (code=%d)", pthread_ret);
    _cleanup_(cleanup_mutex_unlockp)
    pthread_mutex_t *locked_mutex = &worker->mutex;

    *done = !worker->busy;
    if (worker->stop) {
        *done = true;
        *result = VRTD_RET_INTERNAL_ERROR;
    } else {
        *result = worker->result;
    }

    return 0;
}

bool flash_worker_is_busy(struct flash_worker *worker)
{
    if (worker == NULL) {
        return false;
    }

    (void) pthread_mutex_lock(&worker->mutex);
    bool busy = worker->busy;
    (void) pthread_mutex_unlock(&worker->mutex);
    return busy;
}

void cleanup_flash_worker(struct flash_worker *worker)
{
    if (worker == NULL) {
        return;
    }

    flash_worker_release_resources(worker);
    free(worker);
}
