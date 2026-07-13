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
 * @file flash.c
 * @brief AMI cfgmem programming backend for vrtd.
 */

#define _GNU_SOURCE

#include "flash.h"

#include <stdio.h>
#include <sys/syslog.h>

#include <ami.h>
#include <ami_device.h>
#include <ami_program.h>

#include "device.h"
#include "hotplug.h"
#include "reset.h"
#include "utils.h"

uint16_t cfgmem_program_with_ami(
    struct device *device,
    struct device_ptr_array *devices,
    int input_fd,
    uint8_t boot_device,
    uint32_t partition
)
{
    if (device == NULL || devices == NULL || input_fd < 0) {
        return VRTD_RET_BAD_REQUEST;
    }

    char pf0_bdf[VRTD_PCI_BDF_LEN] = {0};
    int ret = pci_bdf_set_function(device->pci_info.bdf, 0, pf0_bdf);
    if (ret != 0) {
        LOG(LOG_ERR, "cfgmem_program_with_ami: failed to compute PF0 BDF from %s", device->pci_info.bdf);
        return VRTD_RET_INTERNAL_ERROR;
    }

    struct ami_device *ami_device = NULL;
    ret = ami_dev_find(pf0_bdf, &ami_device);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "cfgmem_program_with_ami: ami_dev_find(%s) failed: %s", pf0_bdf, ami_get_last_error());
        return VRTD_RET_INTERNAL_ERROR;
    }

    ret = ami_dev_request_access(ami_device);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "cfgmem_program_with_ami: ami_dev_request_access(%s) failed: %s", pf0_bdf, ami_get_last_error());
        ami_dev_delete(&ami_device);
        return VRTD_RET_INTERNAL_ERROR;
    }

    char fd_path[64] = {0};
    ret = snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", input_fd);
    if (ret < 0 || (size_t)ret >= sizeof(fd_path)) {
        LOG(LOG_ERR, "cfgmem_program_with_ami: failed to build fd path for fd %d", input_fd);
        ami_dev_delete(&ami_device);
        return VRTD_RET_INTERNAL_ERROR;
    }

    LOG(LOG_INFO, "cfgmem_program_with_ami: programming %s boot_device=%u partition=%u",
        pf0_bdf, (unsigned int)boot_device, (unsigned int)partition);

    ret = ami_prog_download_pdi(ami_device, fd_path, boot_device, partition, NULL);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "cfgmem_program_with_ami: ami_prog_download_pdi(%s, partition=%u) failed: %s",
            pf0_bdf, (unsigned int)partition, ami_get_last_error());
        ami_dev_delete(&ami_device);
        return VRTD_RET_INTERNAL_ERROR;
    }

    ami_dev_delete(&ami_device);

    LOG(LOG_INFO, "cfgmem_program_with_ami: programming complete for %s, resetting into partition %u",
        pf0_bdf, (unsigned int)partition);

    return reset_with_ami_partition(device, devices, partition);
}
