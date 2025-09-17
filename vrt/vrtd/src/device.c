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

#include "device.h"

#include <assert.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <systemd/sd-journal.h>

static int devices_open(struct device_ptr_array *devices, size_t pathc, char ** paths);
static int device_open(struct device *d, const char *path);
void cleanup_device(struct device *d);

int devices_discover_and_open(struct device_ptr_array *devices)
{
    _cleanup_(globfree)
    glob_t g = {0};

    int ret = glob("/dev/slash_ctl*", GLOB_ERR, NULL, &g);
    if (ret != 0) {
        (void) sd_journal_print(
            LOG_ERR,
            "Error matching pattern /dev/slash_ctl*: %s",
            glob_err_to_string(ret)
        );

        return -1;
    }

    return devices_open(devices, g.gl_pathc, g.gl_pathv);
}

static int devices_open(struct device_ptr_array *devices, size_t pathc, char ** paths)
{
    for (size_t i = 0; i < pathc; ++i) {
        const char *path = paths[i];

        _cleanup_(cleanup_devicep)
        struct device *d = calloc(1, sizeof *d);
        PROPAGATE_ERROR_NULL_STDC_LOG(d, LOG_ERR, "Failed to allocate memory for device data");

        int ret = device_open(d, path);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to open device %s", path);

        ret = device_ptr_array_push_move(devices, &d);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to allocate memory for device data");
    }

    return 0;
}

static int device_open(struct device *d, const char *path)
{
    d->path = strdup(path);
    PROPAGATE_ERROR_NULL_STDC_LOG(d->path, LOG_ERR, "Failed to allocate memory for device data");

    enum slash_error err = SLASH_ERROR_OK;
    d->ctl = slash_ctldev_open(path, &err);
    if (err != SLASH_ERROR_OK) {
        (void) sd_journal_print(
            LOG_ERR,
            "Error opening device %s",
            path
        );
        free(d->path);
        return -1;
    }

    assert(d->ctl != NULL);

    for (size_t i = 0; i < SIZEOF_ARRAY(d->bar_info); i++) {
        d->bar_info[i] = slash_bar_info_read(d->ctl, i, &err);
        if (err != SLASH_ERROR_OK) {
            (void) sd_journal_print(
                LOG_ERR,
                "Error opening bar_info %zu on device %s",
                i, d->path
            );
            continue;
        }

        assert(d->bar_info[i] != NULL);

        if (d->bar_info[i]->usable) {
            d->bar_files[i] = slash_bar_file_open(d->ctl, i, O_CLOEXEC, &err);
            if (err != SLASH_ERROR_OK) {
                (void) sd_journal_print(
                    LOG_ERR,
                    "Error opening bar_file %zu on device %s",
                    i, d->path
                );
            }
        }
    }

    return 0;   
}

void cleanup_device(struct device *d)
{
    if (d == NULL) {
        return;
    }

    enum slash_error err = SLASH_ERROR_OK;

    /* Close any opened BAR files */
    for (size_t i = 0; i < SIZEOF_ARRAY(d->bar_files); i++) {
        if (d->bar_files[i] != NULL) {
            slash_bar_file_close(d->bar_files[i], &err);
            if (err != SLASH_ERROR_OK) {
                (void) sd_journal_print(
                    LOG_WARNING,
                    "Error closing bar_file %zu for %s: ignoring",
                    i, d->path ? d->path : "(unknown)"
                );
            }
            d->bar_files[i] = NULL;
        }
    }

    /* Free bar info data */
    for (size_t i = 0; i < SIZEOF_ARRAY(d->bar_info); i++) {
        if (d->bar_info[i] != NULL) {
            slash_bar_info_free(d->bar_info[i], &err);
            if (err != SLASH_ERROR_OK) {
                (void) sd_journal_print(
                    LOG_WARNING,
                    "Error closing bar_info %zu for %s: ignoring",
                    i, d->path ? d->path : "(unknown)"
                );
            }
            d->bar_info[i] = NULL;
        }
    }

    /* Close control device last */
    if (d->ctl != NULL) {
        slash_ctldev_close(d->ctl, &err);
        if (err != SLASH_ERROR_OK) {
            (void) sd_journal_print(
                LOG_WARNING,
                "Error closing ctldevice %s: ignoring",
                d->path ? d->path : "(unknown)"
            );
        }
        d->ctl = NULL;
    }

    free(d->path);
    d->path = NULL;

    free(d);
}

