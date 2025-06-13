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

static void toggle_sbr(struct pcie_hotplug_device *dev) {
    u16 ctrl;
    struct pci_dev* pdev = NULL;
    printk(KERN_INFO "Toggling SBR for device: %s\n", dev->bdf);
    pdev = get_pci_dev_by_bdf(dev->rootport_bdf);
    if (!pdev) {
        return;
    }

    // Read the PCI control register
    pci_read_config_word(pdev, PCI_BRIDGE_CONTROL, &ctrl);

    // set SBR - PDI will reload here
    ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
    pci_write_config_word(pdev, PCI_BRIDGE_CONTROL, ctrl);

    msleep(2); // small delay before resetting the SBR

    // clear SBR
    ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
    pci_write_config_word(pdev, PCI_BRIDGE_CONTROL, ctrl);

    msleep(5000);
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


static void pcie_hotplug_remove(struct pci_dev *pdev) {
    struct pcie_hotplug_device *dev = pci_get_drvdata(pdev);

    if (!dev)
        return;

    device_destroy(pcie_hotplug_class, dev->devt);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, 1);
    kfree(dev->bdf);
    kfree(dev);

    printk(KERN_INFO "PCIe hotplug remove\n");
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = pcie_hotplug_open,
    .release = pcie_hotplug_release,
    .unlocked_ioctl = pcie_hotplug_ioctl,
};

static long pcie_hotplug_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct pcie_hotplug_device *dev = file->private_data;

    switch (cmd) {
        case PCIE_IOCTL_RESCAN:
            handle_rescan();
            break;
        case PCIE_IOCTL_REMOVE:
            handle_pcie_remove(dev);
            break;
        case PCIE_IOCTL_TOGGLE_SBR:
            toggle_sbr(dev);
            break;
        case PCIE_IOCTL_HOTPLUG:
            handle_pcie_hotplug(dev);
            break;
        default:
            printk(KERN_WARNING "Invalid IOCTL command: 0x%x\n", cmd);
            return -EINVAL;
    }

    return 0;
}

static int pcie_hotplug_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int ret;
    char bdf[16];
    struct pcie_hotplug_device *dev;

    ret = pci_enable_device(pdev);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%x",
             pci_domain_nr(pdev->bus),
             pdev->bus->number,
             PCI_SLOT(pdev->devfn),
             PCI_FUNC(pdev->devfn));

    dev->bdf = kstrdup(bdf, GFP_KERNEL);
    if (!dev->bdf) {
        kfree(dev);
        return -ENOMEM;
    }

    ret = get_bdfs(dev->bdf, dev->rootport_bdf);
    if (ret < 0)
        goto err_bdf;

    ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
    if (ret < 0)
        goto err_bdf;

    cdev_init(&dev->cdev, &fops);
    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret < 0)
        goto err_chrdev;

    device_create(pcie_hotplug_class, NULL, dev->devt, NULL, "pcie_hotplug_%s", dev->bdf);

    pci_set_drvdata(pdev, dev);  // link device struct to PCI dev
    list_add(&dev->list, &device_list);
    device_count++;

    printk(KERN_INFO "PCIe hotplug probe: %s\n", dev->bdf);
    return 0;

err_chrdev:
    unregister_chrdev_region(dev->devt, 1);
err_bdf:
    kfree(dev->bdf);
    kfree(dev);
    return ret;

}

// static void add_device(const char *bdf) {
//     struct pcie_hotplug_device *dev;
//     int ret;

//     dev = kzalloc(sizeof(*dev), GFP_KERNEL);
//     if (!dev) {
//         return;
//     }

//     dev->bdf = kstrdup(bdf, GFP_KERNEL);
//     if (!dev->bdf) {
//         kfree(dev);
//         return;
//     }

//     // Find root port for the device
//     ret = get_bdfs(dev->bdf, dev->rootport_bdf);
//     if (ret < 0) {
//         kfree(dev->bdf);
//         kfree(dev);
//         return;
//     }

//     ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
//     if (ret < 0) {
//         kfree(dev->bdf);
//         kfree(dev);
//         return;
//     }

//     cdev_init(&dev->cdev, &fops);
//     dev->cdev.owner = THIS_MODULE;
//     ret = cdev_add(&dev->cdev, dev->devt, 1);
//     if (ret < 0) {
//         unregister_chrdev_region(dev->devt, 1);
//         kfree(dev->bdf);
//         kfree(dev);
//         return;
//     }

//     device_create(pcie_hotplug_class, NULL, dev->devt, NULL, "pcie_hotplug_%s", dev->bdf);

//     list_add(&dev->list, &device_list);
//     device_count++;

//     printk(KERN_INFO "Added PCIe hotplug device: %s, root port: %s\n", dev->bdf, dev->rootport_bdf);
// }

static struct pci_driver pcie_hotplug_driver = {
    .name = "pcie_hotplug",
    .id_table = pcie_hotplug_ids,
    .probe = pcie_hotplug_probe,
    .remove = pcie_hotplug_remove,
};

static int __init pcie_hotplug_init(void) {
    int ret;
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

    ret = pci_register_driver(&pcie_hotplug_driver);
    if (ret < 0) {
        class_destroy(pcie_hotplug_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }

    printk(KERN_INFO "PCIe hotplug initialized\n");
    return 0;
}

static void __exit pcie_hotplug_exit(void) {
    struct pcie_hotplug_device *dev, *tmp;
    
    pci_unregister_driver(&pcie_hotplug_driver);
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
MODULE_VERSION("1.1");