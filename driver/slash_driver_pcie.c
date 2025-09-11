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

#include "slash_driver_pcie.h"

#include <linux/module.h>
#include <linux/printk.h>

#include "slash_driver.h"
#include "slash_driver_ctldev.h"

static int slash_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void slash_pcie_remove(struct pci_dev *pdev);

static const struct pci_device_id slash_pcie_ids[] = {
    {PCI_DEVICE(SLASH_PCIE_VENDOR_ID, SLASH_PCIE_DEVICE_ID)},
    {0,}
};
MODULE_DEVICE_TABLE(pci, slash_pcie_ids);

static struct pci_driver slash_pcie_driver = {
    .name = SLASH_DRIVER_NAME,
    .id_table = slash_pcie_ids,
    .probe = slash_pcie_probe,
    .remove = slash_pcie_remove,
};

static int slash_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int err;

    (void) id; /* Unused */

    dev_info(&pdev->dev, "slash: probe start for %s\n", pci_name(pdev));
    dev_dbg(&pdev->dev, "slash: vendor=0x%04x device=0x%04x fn=%u\n", pdev->vendor, pdev->device, PCI_FUNC(pdev->devfn));

    if (PCI_FUNC(pdev->devfn) != SLASH_PCIE_PF) {
        dev_err(&pdev->dev, "slash: expected PF %u, got %u\n", SLASH_PCIE_PF, PCI_FUNC(pdev->devfn));
        return -EINVAL;
    }

    pci_dev_get(pdev);

    err = pci_enable_device(pdev);
    if (err) {
        dev_err(&pdev->dev, "slash: pci_enable_device() failed: %d\n", err);
        goto err_put_device;
    }

    pci_set_master(pdev);
    dev_dbg(&pdev->dev, "slash: bus mastering enabled\n");

    err = slash_ctldev_create(pdev);
    if (err) {
        dev_err(&pdev->dev, "slash: control device create failed: %d\n", err);
        goto err_disable_device;
    }

    dev_info(&pdev->dev, "slash: probe successful\n");  
    return 0;

err_disable_device:
    pci_clear_master(pdev);
    pci_disable_device(pdev);

err_put_device:
    pci_dev_put(pdev);

    return err;
}

static void slash_pcie_remove(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "slash: remove start for %s\n", pci_name(pdev));

    slash_ctldev_destroy(pdev);
    pci_clear_master(pdev);
    pci_disable_device(pdev);
    pci_dev_put(pdev);

    dev_info(&pdev->dev, "slash: remove complete\n");
}

int __init slash_pcie_init(void)
{
    int err;

    pr_info("slash: registering PCIe driver '%s'\n", SLASH_DRIVER_NAME);
    err = pci_register_driver(&slash_pcie_driver);

    if (err) {
        pr_err("slash: pci_register_driver failed: %d\n", err);
        return err;
    }

    pr_info("slash: driver '%s' registered\n", SLASH_DRIVER_NAME);
    return 0;
}

void __exit slash_pcie_exit(void)
{
    pr_info("slash: unregistering PCIe driver '%s'\n", SLASH_DRIVER_NAME);
    pci_unregister_driver(&slash_pcie_driver);
    pr_info("slash: driver '%s' unregistered\n", SLASH_DRIVER_NAME);
}
