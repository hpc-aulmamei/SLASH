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

#include <sys/ioctl.h>

#include <ami.h>
#include <ami_device.h>
#include <ami_device_internal.h>
#include <ami_ioctl.h>
#include <ami_mem_access.h>

#include "device.h"
#include "hotplug.h"
#include "utils.h"

uint16_t reset_with_ami(struct device *device, struct device_ptr_array  *devices)
{
    char pf0_bdf[VRTD_PCI_BDF_LEN] = {0};
    char pf1_bdf[VRTD_PCI_BDF_LEN] = {0};
    char pf2_bdf[VRTD_PCI_BDF_LEN] = {0};

    struct ami_device *ami_device = NULL;

    int ret = pci_bdf_set_function(device->pci_info.bdf, 0, pf0_bdf);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: failed to compute PF0 BDF from %s", device->pci_info.bdf);
        return VRTD_RET_INTERNAL_ERROR;
    }
    ret = pci_bdf_set_function(device->pci_info.bdf, 1, pf1_bdf);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: failed to compute PF1 BDF from %s", device->pci_info.bdf);
        return VRTD_RET_INTERNAL_ERROR;
    }
    ret = pci_bdf_set_function(device->pci_info.bdf, 2, pf2_bdf);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: failed to compute PF2 BDF from %s", device->pci_info.bdf);
        return VRTD_RET_INTERNAL_ERROR;
    }

    // We are now removing this device.
    device_ptr_array_rm_by_reference(devices, device);
    device = NULL;

    // PF0 is AVED/AMI bdf
    ret = ami_dev_find(pf0_bdf, &ami_device);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "reset_with_ami: ami_dev_find(%s) failed: %s", pf0_bdf, ami_get_last_error());
        return VRTD_RET_INTERNAL_ERROR;
    }

    ret = ami_dev_request_access(ami_device);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "reset_with_ami: ami_dev_request_access(%s) failed: %s", pf0_bdf, ami_get_last_error());
        ami_dev_delete(&ami_device);
        return VRTD_RET_INTERNAL_ERROR;
    }

    /*
     * We issue AMI_IOC_DEVICE_BOOT directly rather than calling
     * ami_prog_device_boot(), even though the latter is the intended public
     * API for this operation.  The reason is that ami_prog_device_boot()
     * unconditionally calls ami_dev_hot_reset() after the ioctl succeeds.
     * ami_dev_hot_reset() performs its own full remove-device / toggle-SBR /
     * rescan cycle by opening the PCIe bridge config-space sysfs file
     * (/sys/bus/pci/devices/<port>/config) with O_RDWR.  That file is mode
     * 0600 and owned by root; vrtd runs as the unprivileged 'vrtd' user, so
     * the open always fails with EBADF regardless of any Linux capabilities
     * granted to the process.
     *
     * More fundamentally, even if the open succeeded, ami_dev_hot_reset would
     * conflict with vrtd's own hotplug reset sequence that follows immediately
     * below.  vrtd drives hotplug through the slash kernel module
     * (slash_hotplug_remove / slash_hotplug_toggle_sbr / slash_hotplug_rescan),
     * which is the authoritative hotplug path for SLASH devices.  Letting both
     * ami_dev_hot_reset and the slash hotplug sequence run would reset the
     * device twice and leave the AMI device handle in an inconsistent state.
     *
     * The correct behaviour is to issue only the AMI_IOC_DEVICE_BOOT ioctl to
     * inform the AMC firmware of the desired boot partition, then hand control
     * back to vrtd to drive the full hotplug sequence itself.  We set
     * cap_override from the device handle (populated earlier by
     * ami_dev_request_access) so that the kernel driver's per-ioctl permission
     * check passes for the unprivileged vrtd user.  CAP_DAC_OVERRIDE granted
     * via AmbientCapabilities in vrtd.service provides an additional fallback
     * that satisfies the IS_ROOT_USER() check in the driver independently of
     * cap_override.
     */
    {
        struct ami_ioc_data_payload boot_payload = { 0 };
        boot_payload.partition = 1;
        boot_payload.cap_override = ami_device->cap_override;

        if (ami_open_cdev(ami_device) != AMI_STATUS_OK) {
            LOG(LOG_ERR, "reset_with_ami: ami_open_cdev(%s) failed: %s", pf0_bdf, ami_get_last_error());
            ami_dev_delete(&ami_device);
            return VRTD_RET_INTERNAL_ERROR;
        }

        errno = 0;
        if (ioctl(ami_device->cdev, AMI_IOC_DEVICE_BOOT, &boot_payload) != 0) {
            LOG(LOG_ERR, "reset_with_ami: AMI_IOC_DEVICE_BOOT(%s) failed: errno %d (%s)",
                pf0_bdf, errno, strerror(errno));
            ami_dev_delete(&ami_device);
            return VRTD_RET_INTERNAL_ERROR;
        }
    }

    ret = ami_mem_bar_write(ami_device, 0, 0x1040000, 1);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "reset_with_ami: ami_mem_bar_write(%s) failed: %s", pf0_bdf, ami_get_last_error());
        ami_dev_delete(&ami_device);
        return VRTD_RET_INTERNAL_ERROR;
    }

    ami_dev_delete(&ami_device);

    ret = slash_hotplug_remove(g_hotplug, pf0_bdf);
    if (ret != 0 && errno != ENODEV) {
        LOG(LOG_ERR, "reset_with_ami: hotplug remove(%s) failed: %m", pf0_bdf);
        return hotplug_errno_to_vrtd_ret(errno);
    }
    ret = slash_hotplug_remove(g_hotplug, pf1_bdf);
    if (ret != 0 && errno != ENODEV) {
        LOG(LOG_ERR, "reset_with_ami: hotplug remove(%s) failed: %m", pf1_bdf);
        return hotplug_errno_to_vrtd_ret(errno);
    }
    ret = slash_hotplug_remove(g_hotplug, pf2_bdf);
    if (ret != 0 && errno != ENODEV) {
        LOG(LOG_ERR, "reset_with_ami: hotplug remove(%s) failed: %m", pf2_bdf);
        return hotplug_errno_to_vrtd_ret(errno);
    }

    ret = slash_hotplug_toggle_sbr(g_hotplug, pf0_bdf);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: hotplug toggle_sbr(%s) failed: %m", pf0_bdf);
        return hotplug_errno_to_vrtd_ret(errno);
    }

    usleep(5000000);

    // Rescan device after reset. This should re-introduce the devices.
    ret = slash_hotplug_rescan(g_hotplug);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: hotplug rescan failed: %m");
        return hotplug_errno_to_vrtd_ret(errno);
    }

    // Test that we found the newly added device
    ret = ami_dev_find(pf0_bdf, &ami_device);
    if (ret != AMI_STATUS_OK) {
        LOG(LOG_ERR, "reset_with_ami: post-reset ami_dev_find(%s) failed: %s", pf0_bdf, ami_get_last_error());
        return VRTD_RET_INTERNAL_ERROR;
    }

    ami_dev_delete(&ami_device);

    // We now rescan for the reset device
    ret = devices_discover_and_open(devices);
    if (ret != 0) {
        LOG(LOG_ERR, "reset_with_ami: devices_discover_and_open failed after reset");
        return VRTD_RET_INTERNAL_ERROR;
    }

    return VRTD_RET_OK;
}
