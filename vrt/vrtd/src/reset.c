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

#include "reset.h"

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include <ami.h>
#include <ami_device.h>
#include <ami_mem_access.h>
#include <ami_program.h>

#include "device.h"
#include "hotplug.h"

uint16_t reset_with_ami(struct device *device, struct device_ptr_array  *devices)
{
    char pf0_bdf[VRTD_PCI_BDF_LEN] = {0};
    char pf1_bdf[VRTD_PCI_BDF_LEN] = {0};
    char pf2_bdf[VRTD_PCI_BDF_LEN] = {0};

    struct ami_device *ami_device = NULL;

    int ret = pci_bdf_set_function(device->pci_info.bdf, 0, pf0_bdf);
    if (ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }
    ret = pci_bdf_set_function(device->pci_info.bdf, 1, pf1_bdf);
    if (ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }
    ret = pci_bdf_set_function(device->pci_info.bdf, 2, pf2_bdf);
    if (ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    // We are now removing this device.
    device_ptr_array_rm_by_reference(devices, device);
    device = NULL;

    // PF0 is AVED/AMI bdf
    ret = ami_dev_find(pf0_bdf, &ami_device);
    if (ret != AMI_STATUS_OK) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    ret = ami_dev_request_access(ami_device);
    if (ret != AMI_STATUS_OK) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    ret = ami_prog_device_boot(&ami_device, 1);
    if (ret != AMI_STATUS_OK) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    ret = ami_mem_bar_write(ami_device, 0, 0x1040000, 1);
    if (ret != AMI_STATUS_OK) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    ami_dev_delete(&ami_device);

    ret = slash_hotplug_remove(g_hotplug, pf0_bdf);
    if (ret != 0 && errno != ENODEV) {
        return hotplug_errno_to_vrtd_ret(errno);
    }
    ret = slash_hotplug_remove(g_hotplug, pf1_bdf);
    if (ret != 0 && errno != ENODEV) {
        return hotplug_errno_to_vrtd_ret(errno);
    }
    ret = slash_hotplug_remove(g_hotplug, pf2_bdf);
    if (ret != 0 && errno != ENODEV) {
        return hotplug_errno_to_vrtd_ret(errno);
    }

    ret = slash_hotplug_toggle_sbr(g_hotplug, pf0_bdf);
    if (ret != 0) {
        return hotplug_errno_to_vrtd_ret(errno);
    }

    usleep(5000000);

    // Rescan device after reset. This should re-introduce the devices.
    ret = slash_hotplug_rescan(g_hotplug);
    if (ret != 0) {
        return hotplug_errno_to_vrtd_ret(errno);
    }

    // Test that we found the newly added device
    ret = ami_dev_find(pf0_bdf, &ami_device);
    if (ret != AMI_STATUS_OK) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    ami_dev_delete(&ami_device);

    // We now rescan for the reset device
    ret = devices_discover_and_open(devices);
    if (ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    return VRTD_RET_OK;
}
