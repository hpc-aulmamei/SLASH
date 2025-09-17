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

#ifndef VRTD_CTLDEV_H
#define VRTD_CTLDEV_H

#include <stddef.h>

#include <slash_driver/ctldev.h>

#include "array.h"

struct device {
    char *path; /* owning */
    struct slash_ctldev *ctl;
    struct slash_ioctl_bar_info *bar_info[6];
    struct slash_bar_file *bar_files[6];
};

void cleanup_device(struct device *d);
static inline
void cleanup_devicep(struct device **d)
{
    cleanup_device(*d);

    *d = NULL;
}

DECLARE_OWNING_PTR_ARRAY(device_ptr_array, struct device *, cleanup_device);

int devices_discover_and_open(struct device_ptr_array *devices);

#endif // VRTD_CTLDEV_H
