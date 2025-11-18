/**
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "slash.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/printk.h>

#include "slash_pcie.h"
#include "slash_hotplug_driver.h"
#include "slash_qdma.h"

static unsigned int qdma_num_threads = 8;
static char *qdma_debugfs_path = NULL;

static int __init slash_init(void)
{
    int err;

    pr_info("slash: module init\n");

    err = slash_qdma_init(qdma_num_threads, NULL);
    if (err) {
        pr_err("slash: libqdma init failed: %d\n", err);
        return err;
    }

    err = slash_hotplug_init();
    if (err) {
        pr_err("slash: hotplug init failed: %d\n", err);
        return err;
    }

    err = slash_pcie_init();
    if (err) {
        pr_err("slash: PCIe init failed: %d\n", err);
        slash_hotplug_exit();
        return err;
    }

    pr_info("slash: module init complete\n");
    return 0;
}

static void __exit slash_exit(void)
{
    pr_info("slash: module exit\n");
    slash_pcie_exit();
    slash_hotplug_exit();
    slash_qdma_exit();
    pr_info("slash: module exit complete\n");
}

module_init(slash_init);
module_exit(slash_exit);

module_param(qdma_num_threads, uint, 0644);
module_param(qdma_debugfs_path, charp, 0644);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("SLASH/VRT module");
MODULE_VERSION("1.0");
