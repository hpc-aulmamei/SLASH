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

#include "pcie_hotplug.h"

#define DEVICE_NAME "pcie_hotplug"
#define CLASS_NAME "pcie"
#define BUF_SIZE 16


static struct pci_dev *get_pci_dev_by_bdf(const char* bdf) {
    int domain, bus, slot, func;
    struct pci_dev* pdev;

    if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4) {
        printk(KERN_ERR "Invalid BDF format\n");
        return NULL;
    }

    pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, func));
    if (!pdev) {
        printk(KERN_ERR "Cannot find PCI device\n");
        return NULL;
    }

    return pdev;
}

static struct pci_dev *get_next_function_pci_dev(const char* bdf) {
    int domain, bus, slot, func;
    char new_bdf[16];
    struct pci_dev* pdev;

    if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4) {
        printk(KERN_ERR "Invalid BDF format\n");
        return NULL;
    }

    // Increment the function number
    func += 1;

    // Construct the new BDF string
    snprintf(new_bdf, sizeof(new_bdf), "%04x:%02x:%02x.%x", domain, bus, slot, func);

    // Get the PCI device with the new BDF
    pdev = get_pci_dev_by_bdf(new_bdf);
    if (!pdev) {
        printk(KERN_ERR "Cannot find PCI device with BDF %s\n", new_bdf);
        return NULL;
    }

    return pdev;
}

static void toggle_sbr(struct pcie_hotplug_device *dev)
{
    struct pci_dev *bridge = NULL, *ep = NULL, *p;
    int domain, bus, slot, func;
    u16 ctrl;

    if (!dev || !dev->bdf) {
        printk(KERN_ERR "toggle_sbr: invalid device\n");
        return;
    }

    /* Parse BDF of the endpoint we want to reset under */
    if (sscanf(dev->bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4) {
        printk(KERN_ERR "toggle_sbr: invalid BDF format: %s\n", dev->bdf);
        return;
    }

    /* First try: if EP is present, get its immediate upstream bridge */
    ep = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, func));
    if (ep) {
        struct pci_dev *up = pci_upstream_bridge(ep);
        if (up)
            bridge = pci_dev_get(up);   /* take ref */
        pci_dev_put(ep);
    }

    /* Fallback: EP may be removed already. Find the bridge whose secondary bus == EP bus. */
    if (!bridge) {
        for_each_pci_dev(p) {
            if (pci_domain_nr(p->bus) != domain)
                continue;
            if (!pci_is_bridge(p) || !p->subordinate)
                continue;
            if (p->subordinate->number == bus) {
                bridge = pci_dev_get(p); /* take ref */
                break;
            }
        }
    }

    if (!bridge) {
        printk(KERN_ERR "toggle_sbr: no upstream bridge found for %s\n", dev->bdf);
        return;
    }

    printk(KERN_INFO "toggle_sbr: pulsing SBR on upstream bridge %04x:%02x:%02x.%x (sec bus %02x)\n",
           pci_domain_nr(bridge->bus),
           bridge->bus->number,
           PCI_SLOT(bridge->devfn),
           PCI_FUNC(bridge->devfn),
           bridge->subordinate ? bridge->subordinate->number : 0xff);

#if IS_ENABLED(CONFIG_PCI)
    /*
     * Prefer core helper; returns 0 on success on modern kernels.
     * If unavailable or it fails, fall back to manual toggle.
     */
    if (!pci_bridge_secondary_bus_reset(bridge)) {
        /* small settle time similar to userspace */
        msleep(300);
        pci_dev_put(bridge);
        return;
    }
#endif

    /* Manual SBR pulse on PCI_BRIDGE_CONTROL */
    pci_read_config_word(bridge, PCI_BRIDGE_CONTROL, &ctrl);
    ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
    pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl);

    msleep(2);   /* short pulse like userspace */

    ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
    pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl);

    msleep(50000);

    pci_dev_put(bridge);
}

static void handle_rescan(void) {
    struct pci_bus* bus;
    printk(KERN_INFO "Rescanning PCIe bus\n");
    list_for_each_entry(bus, &pci_root_buses, node) {
        pci_rescan_bus(bus);
    }
}

static void handle_pcie_remove(struct pcie_hotplug_device *dev) {
    struct pci_dev* device_dev = NULL;
    if(dev->bdf) {
        device_dev = get_pci_dev_by_bdf(dev->bdf);
        if (!device_dev) {
            return;
        }
    }

    if(device_dev) {
        printk(KERN_INFO "Removing PCIe device: %s\n", dev->bdf);
        pci_stop_and_remove_bus_device(device_dev);
    }
}

static void handle_pcie_hotplug(struct pcie_hotplug_device *dev) {
    struct pci_dev* rootport_dev = NULL;
    struct pci_dev* device_dev = NULL;
    struct pci_bus* bus;
    if(dev->bdf) {
        device_dev = get_next_function_pci_dev(dev->bdf);
        if(!device_dev) {
            return;
        }
    }

    if(device_dev) {
        printk(KERN_INFO "Removing PCIe device: %s\n", dev->bdf);
        pci_stop_and_remove_bus_device(device_dev);
    }

    rootport_dev = pcie_find_root_port(device_dev);

    if(!rootport_dev) {
        return;
    } else {
        printk(KERN_INFO "Root port: %04x:%02x:%02x.%x\n",
         pci_domain_nr(rootport_dev->bus),
          rootport_dev->bus->number,
          PCI_SLOT(rootport_dev->devfn),
          PCI_FUNC(rootport_dev->devfn));
    }
    bus = rootport_dev->subordinate;
    
    if(bus) {
        printk(KERN_INFO "Rescanning PCIe bus\n");
        pci_rescan_bus(bus);
    }

}

static int get_bdfs(const char *device_bdf, char *rootport_bdf) {
    struct pci_dev* device_dev = NULL;
    struct pci_dev* rootport_dev = NULL;

    if(device_bdf) {
        device_dev = get_pci_dev_by_bdf(device_bdf);
        if (!device_dev) {
            return -EINVAL;
        }
    }

    rootport_dev = pcie_find_root_port(device_dev);
    snprintf(rootport_bdf, 32, "%04x:%02x:%02x.%x",
            pci_domain_nr(rootport_dev->bus),
            rootport_dev->bus->number,
            PCI_SLOT(rootport_dev->devfn),
            PCI_FUNC(rootport_dev->devfn));
    
    return 0;
}

static int pcie_hotplug_open(struct inode *inode, struct file *file) {
    struct pcie_hotplug_device *dev;

    dev = container_of(inode->i_cdev, struct pcie_hotplug_device, cdev);
    file->private_data = dev;

    return 0;
}

static int pcie_hotplug_release(struct inode *inode, struct file *file) {
    return 0;
}

static ssize_t pcie_hotplug_write(struct file* file, const char __user* buffer, size_t len, loff_t* offset) {
    char cmd[16];
    struct pcie_hotplug_device *dev = file->private_data;

    if(len > 15) {
        return -EINVAL;
    }

    if(copy_from_user(cmd, buffer, len)) {
        return -EFAULT;
    }

    cmd[len] = '\0';
    printk(KERN_INFO "Received command: %s\n", cmd);

    if(strncmp(cmd, "rescan", 6) == 0) {
        handle_rescan();
    } else if(strncmp(cmd, "remove", 6) == 0) {
        handle_pcie_remove(dev);
    } else if(strncmp(cmd, "toggle_sbr", 10) == 0) {
        toggle_sbr(dev);
    } else if(strncmp(cmd, "hotplug", 7) == 0) {
        handle_pcie_hotplug(dev);
    } else {
        printk(KERN_WARNING "Invalid command\n");
    }

    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = pcie_hotplug_open,
    .release = pcie_hotplug_release,
    .write = pcie_hotplug_write,
};

static void discover_and_add_devices(void) {
    struct pci_dev *pdev = NULL;
    char bdf[16];
    const struct pci_device_id *id;

    for_each_pci_dev(pdev) {
        for (id = pcie_hotplug_ids; id->vendor != 0; id++) {
            if (pdev->vendor == id->vendor && pdev->device == id->device && PCI_FUNC(pdev->devfn) == 0) {
                snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%x",
                         pci_domain_nr(pdev->bus),
                         pdev->bus->number,
                         PCI_SLOT(pdev->devfn),
                         PCI_FUNC(pdev->devfn));
                add_device(bdf);
            }
        }
    }
}

static void add_device(const char *bdf) {
    struct pcie_hotplug_device *dev;
    int ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        return;
    }

    dev->bdf = kstrdup(bdf, GFP_KERNEL);
    if (!dev->bdf) {
        kfree(dev);
        return;
    }

    // Find root port for the device
    ret = get_bdfs(dev->bdf, dev->rootport_bdf);
    if (ret < 0) {
        kfree(dev->bdf);
        kfree(dev);
        return;
    }

    ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        kfree(dev->bdf);
        kfree(dev);
        return;
    }

    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev->devt, 1);
        kfree(dev->bdf);
        kfree(dev);
        return;
    }

    device_create(pcie_hotplug_class, NULL, dev->devt, NULL, "pcie_hotplug_%s", dev->bdf);

    list_add(&dev->list, &device_list);
    device_count++;

    printk(KERN_INFO "Added PCIe hotplug device: %s, root port: %s\n", dev->bdf, dev->rootport_bdf);
}

static int __init pcie_hotplug_init(void) {

    // Register character device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "Failed to register chrdev\n");
        return major_number;
    }

    // Initialize class
    pcie_hotplug_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(pcie_hotplug_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR "Failed to create class\n");
        return PTR_ERR(pcie_hotplug_class);
    }

    // Discover and add devices with the specified vendor ID
    discover_and_add_devices();

    printk(KERN_INFO "PCIe hotplug initialized\n");
    return 0;
}

static void __exit pcie_hotplug_exit(void) {
    struct pcie_hotplug_device *dev, *tmp;

    list_for_each_entry_safe(dev, tmp, &device_list, list) {
        device_destroy(pcie_hotplug_class, dev->devt);
        cdev_del(&dev->cdev);
        unregister_chrdev_region(dev->devt, 1);
        kfree(dev->bdf);
        kfree(dev);
    }

    class_unregister(pcie_hotplug_class);
    class_destroy(pcie_hotplug_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "pcie_hotplug module unloaded\n");
}

module_init(pcie_hotplug_init);
module_exit(pcie_hotplug_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("PCIe hotplug module");
MODULE_VERSION("1.0");