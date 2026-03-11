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

#define _GNU_SOURCE

#include "hotplug.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

// Global
struct slash_hotplug *g_hotplug = NULL;

void hotplug_global_init(void)
{
    g_hotplug = slash_hotplug_open(NULL);
}

void hotplug_global_destroy(void)
{
    slash_hotplug_close(g_hotplug);
    g_hotplug = NULL;
}

uint16_t hotplug_errno_to_vrtd_ret(int err)
{
    switch (err) {
    case EINVAL:
        return VRTD_RET_INVALID_ARGUMENT;
    case ENODEV:
        return VRTD_RET_NOEXIST;
    case EBUSY:
        return VRTD_RET_BUSY;
    case EPERM:
    case EACCES:
        return VRTD_RET_AUTH_ERROR;
    default:
        return VRTD_RET_INTERNAL_ERROR;
    }
}

int pci_bdf_prefix(const char *bdf, char out_prefix[VRTD_PCI_BDF_LEN])
{
    if (bdf == NULL || out_prefix == NULL) {
        errno = EINVAL;
        return -1;
    }

    const char *dot = strrchr(bdf, '.');
    if (dot == NULL || dot == bdf) {
        errno = EINVAL;
        return -1;
    }

    size_t prefix_len = (size_t)(dot - bdf);
    if (prefix_len >= VRTD_PCI_BDF_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(out_prefix, bdf, prefix_len);
    out_prefix[prefix_len] = '\0';

    return 0;
}

int pci_bdf_set_function(const char *bdf, uint8_t func, char out_bdf[VRTD_PCI_BDF_LEN])
{
    if (bdf == NULL || out_bdf == NULL || func > 7) {
        errno = EINVAL;
        return -1;
    }

    size_t len = strnlen(bdf, VRTD_PCI_BDF_LEN);
    if (len == 0 || len >= VRTD_PCI_BDF_LEN) {
        errno = EINVAL;
        return -1;
    }

    const char *dot = strrchr(bdf, '.');
    if (dot == NULL || dot == bdf || dot[1] == '\0' || dot[2] != '\0') {
        errno = EINVAL;
        return -1;
    }

    size_t prefix_len = (size_t)(dot - bdf);
    if (prefix_len + 2 >= VRTD_PCI_BDF_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(out_bdf, bdf, prefix_len);
    out_bdf[prefix_len] = '.';
    out_bdf[prefix_len + 1] = (char)('0' + func);
    out_bdf[prefix_len + 2] = '\0';

    return 0;
}
