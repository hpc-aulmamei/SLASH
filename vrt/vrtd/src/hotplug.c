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
 * @file hotplug.c
 * @brief PCIe hotplug operations wrapper and PCI BDF string utilities.
 *
 * This module manages the global slash_hotplug handle used to perform PCIe
 * hotplug operations (secondary bus reset, device removal/rescan) on V80
 * FPGA cards.  Because the hotplug subsystem operates on the host's PCIe
 * topology (not a per-device resource), a single global handle is sufficient.
 *
 * The file also provides helper functions for manipulating PCI Bus/Device/
 * Function (BDF) address strings (e.g. "0000:65:00.2"), which are used
 * throughout vrtd when addressing specific PCIe functions of a multi-function
 * FPGA device.
 */

#define _GNU_SOURCE

#include "hotplug.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

/* Global hotplug handle -- singleton because the hotplug subsystem is
 * host-wide, not per-device. */
struct slash_hotplug *g_hotplug = NULL;

/**
 * Open the global hotplug handle.  Called once at daemon startup from
 * globals_init().  The NULL argument requests the default hotplug
 * provider (sysfs-based on Linux).
 */
void hotplug_global_init(void)
{
    g_hotplug = slash_hotplug_open(NULL);
}

/**
 * Close the global hotplug handle and NULL the pointer.  Called once at
 * daemon shutdown from globals_destroy().
 */
void hotplug_global_destroy(void)
{
    slash_hotplug_close(g_hotplug);
    g_hotplug = NULL;
}

/**
 * Translate a POSIX errno from the hotplug subsystem into a vrtd wire
 * protocol return code.  This keeps the errno-to-wire mapping in one
 * place so that all hotplug-related request handlers return consistent
 * error codes to clients.
 */
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

/**
 * Extract the BDF prefix (domain:bus:device) from a full BDF string,
 * stripping the ".function" suffix.
 *
 * Example: "0000:65:00.2" -> "0000:65:00"
 *
 * This is used when operating on the PCIe slot as a whole (e.g. secondary
 * bus reset affects all functions under the same bus:device).
 *
 * @param bdf         NUL-terminated BDF string.
 * @param out_prefix  Output buffer of at least VRTD_PCI_BDF_LEN bytes.
 * @return 0 on success, -1 with errno set on failure.
 */
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

/**
 * Produce a new BDF string with a different function number.
 *
 * Example: pci_bdf_set_function("0000:65:00.0", 2, out) -> out = "0000:65:00.2"
 *
 * V80 FPGA devices expose multiple PCIe functions (e.g. function 0 for QDMA,
 * function 1 for management).  This helper lets the daemon address a sibling
 * function given any BDF from the same device.
 *
 * @param bdf      NUL-terminated source BDF string.
 * @param func     New function number (0-7).
 * @param out_bdf  Output buffer of at least VRTD_PCI_BDF_LEN bytes.
 * @return 0 on success, -1 with errno set on failure.
 */
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

    /* Locate the last '.' which separates the device from the function number.
     * Validate that exactly one digit follows it (PCIe functions are 0-7). */
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

    /* Reconstruct: copy the "domain:bus:device" prefix, then append
     * ".N" where N is the requested function number. */
    memcpy(out_bdf, bdf, prefix_len);
    out_bdf[prefix_len] = '.';
    out_bdf[prefix_len + 1] = (char)('0' + func);
    out_bdf[prefix_len + 2] = '\0';

    return 0;
}
