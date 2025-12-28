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
#include <linux/version.h>

#ifdef RHEL_RELEASE_CODE
#include <linux/rhelversion.h>
#endif

#if (defined(LINUX_VERSION_CODE) && defined(KERNEL_VERSION) && \
     (LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0))) || \
    (defined(RHEL_RELEASE_CODE) && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,0)))
    /* New API: class_create(const char *name) */
#   define CLASS_CREATE(name) class_create(name)
#else
    /* Old API: class_create(struct module *, const char *name) */
#   define CLASS_CREATE(name) class_create(THIS_MODULE, name)
#endif

// #   define CLASS_CREATE(name) class_create(THIS_MODULE, name)


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
    struct pci_dev *ep = NULL, *bridge = NULL;
    struct pci_bus *root;
    int domain, bus, slot, func;
    u16 ctrl;
    unsigned long t0 = jiffies;

    /* configurable delays to match userspace; provide sane defaults */
#ifndef HOT_RESET_GPIO_SET_DELAY_MS
# define HOT_RESET_GPIO_SET_DELAY_MS   20
#endif
#ifndef HOT_RESET_SBR_SET_DELAY_MS
# define HOT_RESET_SBR_SET_DELAY_MS    2
#endif
#ifndef HOT_RESET_RESCAN_DELAY_MS
# define HOT_RESET_RESCAN_DELAY_MS     300
#endif

    printk(KERN_INFO "toggle_sbr: ENTER dev=%p bdf=%s (t0=%lu)\n",
           dev, dev ? dev->bdf : "(null)", t0);

    if (!dev || !dev->bdf) {
        printk(KERN_ERR "toggle_sbr: invalid device or BDF (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        return;
    }

    if (sscanf(dev->bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4) {
        printk(KERN_ERR "toggle_sbr: invalid BDF format: %s (dt=%ums)\n",
               dev->bdf, jiffies_to_msecs(jiffies - t0));
        return;
    }
    printk(KERN_INFO "toggle_sbr: parsed BDF dom=%04x bus=%02x slot=%02x func=%x (dt=%ums)\n",
           domain, bus, slot, func, jiffies_to_msecs(jiffies - t0));

    /* Resolve EP if present (may already be gone) */
    printk(KERN_INFO "toggle_sbr: resolving EP @ %s (dt=%ums)\n",
           dev->bdf, jiffies_to_msecs(jiffies - t0));
    ep = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, func));

    /* Resolve the *immediate upstream bridge* (same as the sysfs "port") */
    if (ep) {
        printk(KERN_INFO "toggle_sbr: EP PRESENT %04x:%02x:%02x.%x ven=%04x dev=%04x class=0x%06x (dt=%ums)\n",
               pci_domain_nr(ep->bus), ep->bus->number,
               PCI_SLOT(ep->devfn), PCI_FUNC(ep->devfn),
               ep->vendor, ep->device, ep->class,
               jiffies_to_msecs(jiffies - t0));
        {
            struct pci_dev *up = pci_upstream_bridge(ep);
            if (up) {
                bridge = pci_dev_get(up);
                printk(KERN_INFO "toggle_sbr: upstream bridge via pci_upstream_bridge(): %04x:%02x:%02x.%x (dt=%ums)\n",
                       pci_domain_nr(bridge->bus), bridge->bus->number,
                       PCI_SLOT(bridge->devfn), PCI_FUNC(bridge->devfn),
                       jiffies_to_msecs(jiffies - t0));
            } else {
                printk(KERN_INFO "toggle_sbr: pci_upstream_bridge() returned NULL (dt=%ums)\n",
                       jiffies_to_msecs(jiffies - t0));
            }
        }
    } else {
        printk(KERN_INFO "toggle_sbr: EP ABSENT at %s (dt=%ums)\n",
               dev->bdf, jiffies_to_msecs(jiffies - t0));
    }

    /* Fallback without walking global device list: use bus->self */
    if (!bridge) {
        struct pci_bus *ep_bus;

        printk(KERN_INFO "toggle_sbr: fallback via pci_find_bus(dom=%04x,bus=%02x) (dt=%ums)\n",
               domain, bus, jiffies_to_msecs(jiffies - t0));

        ep_bus = pci_find_bus(domain, bus);
        if (!ep_bus) {
            printk(KERN_ERR "toggle_sbr: pci_find_bus() returned NULL for %04x:%02x (dt=%ums)\n",
                   domain, bus, jiffies_to_msecs(jiffies - t0));
        } else if (!ep_bus->self) {
            printk(KERN_ERR "toggle_sbr: bus %02x has no upstream bridge (root bus?) (dt=%ums)\n",
                   bus, jiffies_to_msecs(jiffies - t0));
        } else {
            bridge = pci_dev_get(ep_bus->self);
            printk(KERN_INFO "toggle_sbr: found bridge via bus->self: %04x:%02x:%02x.%x (sec=%02x) (dt=%ums)\n",
                   pci_domain_nr(bridge->bus), bridge->bus->number,
                   PCI_SLOT(bridge->devfn), PCI_FUNC(bridge->devfn),
                   bridge->subordinate ? bridge->subordinate->number : 0xff,
                   jiffies_to_msecs(jiffies - t0));
        }
    }

    if (!bridge) {
        if (ep) pci_dev_put(ep);
        printk(KERN_ERR "toggle_sbr: NO upstream bridge for %s — abort (dt=%ums)\n",
               dev->bdf, jiffies_to_msecs(jiffies - t0));
        return;
    }

    printk(KERN_INFO "toggle_sbr: using bridge %04x:%02x:%02x.%x (secondary=%02x) (dt=%ums)\n",
           pci_domain_nr(bridge->bus), bridge->bus->number,
           PCI_SLOT(bridge->devfn), PCI_FUNC(bridge->devfn),
           bridge->subordinate ? bridge->subordinate->number : 0xff,
           jiffies_to_msecs(jiffies - t0));

    /* 1) REMOVE the endpoint under the PCI remove/rescan lock — like sysfs */
    if (ep) {
        printk(KERN_INFO "toggle_sbr: acquiring rescan/remove lock to delete EP (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_lock_rescan_remove();

        printk(KERN_INFO "toggle_sbr: clearing bus master on EP (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_clear_master(ep);

        printk(KERN_INFO "toggle_sbr: calling pci_stop_and_remove_bus_device_locked(ep) (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_stop_and_remove_bus_device_locked(ep);

        printk(KERN_INFO "toggle_sbr: releasing rescan/remove lock after EP removal (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_unlock_rescan_remove();

        printk(KERN_INFO "toggle_sbr: EP removed; dropping EP ref (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_dev_put(ep);
        ep = NULL;
    } else {
        printk(KERN_INFO "toggle_sbr: EP already absent — skipping removal (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
    }

    /* (userspace did a small guard delay after GPIO poke) */
    printk(KERN_INFO "toggle_sbr: sleeping %d ms before SBR (dt=%ums)\n",
           HOT_RESET_GPIO_SET_DELAY_MS, jiffies_to_msecs(jiffies - t0));
    msleep(HOT_RESET_GPIO_SET_DELAY_MS);
    printk(KERN_INFO "toggle_sbr: woke from pre-SBR sleep (dt=%ums)\n",
           jiffies_to_msecs(jiffies - t0));

    /* 2) PULSE SBR on the bridge — *NO PCI LOCK HELD* (matches sysfs fd write) */
#if IS_ENABLED(CONFIG_PCI)
    printk(KERN_INFO "toggle_sbr: attempting pci_bridge_secondary_bus_reset() (dt=%ums)\n",
           jiffies_to_msecs(jiffies - t0));
    if (!pci_bridge_secondary_bus_reset(bridge)) {
        printk(KERN_INFO "toggle_sbr: pci_bridge_secondary_bus_reset() OK; settle sleep %d ms (dt=%ums)\n",
               HOT_RESET_SBR_SET_DELAY_MS + HOT_RESET_RESCAN_DELAY_MS,
               jiffies_to_msecs(jiffies - t0));
        msleep(HOT_RESET_SBR_SET_DELAY_MS + HOT_RESET_RESCAN_DELAY_MS);
    } else
#endif
    {
        u16 ctrl_before = 0, ctrl_after = 0;
        printk(KERN_INFO "toggle_sbr: manual SBR path (dt=%ums)\n",
               jiffies_to_msecs(jiffies - t0));
        pci_read_config_word(bridge, PCI_BRIDGE_CONTROL, &ctrl_before);
        printk(KERN_INFO "toggle_sbr: BRIDGE_CONTROL before=0x%04x (dt=%ums)\n",
               ctrl_before, jiffies_to_msecs(jiffies - t0));

        ctrl = ctrl_before | PCI_BRIDGE_CTL_BUS_RESET;
        pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl);
        printk(KERN_INFO "toggle_sbr: set BUS_RESET; sleep %d ms (dt=%ums)\n",
               HOT_RESET_SBR_SET_DELAY_MS, jiffies_to_msecs(jiffies - t0));
        msleep(HOT_RESET_SBR_SET_DELAY_MS);

        ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
        pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, ctrl);
        pci_read_config_word(bridge, PCI_BRIDGE_CONTROL, &ctrl_after);
        printk(KERN_INFO "toggle_sbr: cleared BUS_RESET; BRIDGE_CONTROL after=0x%04x; settle %d ms (dt=%ums)\n",
               ctrl_after, HOT_RESET_RESCAN_DELAY_MS,
               jiffies_to_msecs(jiffies - t0));
        msleep(HOT_RESET_RESCAN_DELAY_MS);
    }

    printk(KERN_INFO "toggle_sbr: extra settle sleep 5000 ms (dt=%ums)\n",
           jiffies_to_msecs(jiffies - t0));
    msleep(5000);

    /* 3) GLOBAL RESCAN under the PCI lock — exactly like /sys/bus/pci/rescan */
    printk(KERN_INFO "toggle_sbr: BEGIN global rescan (dt=%ums)\n",
           jiffies_to_msecs(jiffies - t0));
    pci_lock_rescan_remove();
    list_for_each_entry(root, &pci_root_buses, node) {
        printk(KERN_INFO "toggle_sbr: pci_rescan_bus(root=%02x) (dt=%ums)\n",
               root->number, jiffies_to_msecs(jiffies - t0));
        pci_rescan_bus(root);
    }
    pci_unlock_rescan_remove();
    printk(KERN_INFO "toggle_sbr: END global rescan (dt=%ums)\n",
           jiffies_to_msecs(jiffies - t0));

    pci_dev_put(bridge);
    printk(KERN_INFO "toggle_sbr: EXIT (total=%ums)\n",
           jiffies_to_msecs(jiffies - t0));
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
        case PCIE_IOCTL_GET_BAR_VAL: {
            struct pcie_bar_read barreq;
            void __iomem *mapped;
            struct pci_dev *pdev = get_pci_dev_by_bdf(dev->bdf);
            if (!pdev)
                return -ENODEV;

            if (copy_from_user(&barreq, (void __user *)arg, sizeof(barreq)))
                return -EFAULT;

            if (barreq.bar_index >= PCI_STD_NUM_BARS)
                return -EINVAL;

            // Sanity check: BAR must exist
            if (!(pci_resource_flags(pdev, barreq.bar_index) & IORESOURCE_MEM))
                return -EINVAL;

            if (barreq.offset >= pci_resource_len(pdev, barreq.bar_index))
                return -EINVAL;

            // Map BAR temporarily
            mapped = pci_iomap(pdev, barreq.bar_index, 0);
            if (!mapped)
                return -ENOMEM;

            barreq.value = ioread32(mapped + barreq.offset);
            pci_iounmap(pdev, mapped);

            if (copy_to_user((void __user *)arg, &barreq, sizeof(barreq)))
                return -EFAULT;

            return 0;
        }

        case PCIE_IOCTL_SET_BAR_VAL: {
            struct pcie_bar_write barreq;
            void __iomem *mapped;
            struct pci_dev *pdev = get_pci_dev_by_bdf(dev->bdf);
            if (!pdev)
                return -ENODEV;

            if (copy_from_user(&barreq, (void __user *)arg, sizeof(barreq)))
                return -EFAULT;

            if (barreq.bar_index >= PCI_STD_NUM_BARS)
                return -EINVAL;

            if (!(pci_resource_flags(pdev, barreq.bar_index) & IORESOURCE_MEM))
                return -EINVAL;

            if (barreq.offset >= pci_resource_len(pdev, barreq.bar_index))
                return -EINVAL;

            mapped = pci_iomap(pdev, barreq.bar_index, 0);
            if (!mapped)
                return -ENOMEM;

            iowrite32(barreq.value, mapped + barreq.offset);

            pci_iounmap(pdev, mapped);
            return 0;
        }

        case PCIE_IOCTL_READ_BAR_RANGE: {
            struct pcie_bar_range range;
            void __iomem *mapped;
            struct pci_dev *pdev = get_pci_dev_by_bdf(dev->bdf);
            if (!pdev)
                return -ENODEV;

            if (copy_from_user(&range, (void __user *)arg, sizeof(range)))
                return -EFAULT;

            if (range.bar_index >= PCI_STD_NUM_BARS ||
                range.size > MAX_BAR_RW_SIZE ||
                range.offset + range.size > pci_resource_len(pdev, range.bar_index))
                return -EINVAL;

            mapped = pci_iomap(pdev, range.bar_index, 0);
            if (!mapped)
                return -ENOMEM;

            memcpy_fromio(range.data, mapped + range.offset, range.size);
            pci_iounmap(pdev, mapped);

            if (copy_to_user((void __user *)arg, &range, sizeof(range)))
                return -EFAULT;

            return 0;
        }

        case PCIE_IOCTL_WRITE_BAR_RANGE: {
            struct pcie_bar_range range;
            void __iomem *mapped;
            struct pci_dev *pdev = get_pci_dev_by_bdf(dev->bdf);
            if (!pdev)
                return -ENODEV;

            if (copy_from_user(&range, (void __user *)arg, sizeof(range)))
                return -EFAULT;

            if (range.bar_index >= PCI_STD_NUM_BARS ||
                range.size > MAX_BAR_RW_SIZE ||
                range.offset + range.size > pci_resource_len(pdev, range.bar_index))
                return -EINVAL;

            mapped = pci_iomap(pdev, range.bar_index, 0);
            if (!mapped)
                return -ENOMEM;

            memcpy_toio(mapped + range.offset, range.data, range.size);
            pci_iounmap(pdev, mapped);

            return 0;
        }



        default:
            printk(KERN_WARNING "Invalid IOCTL command: 0x%x\n", cmd);
            return -EINVAL;
    }

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = pcie_hotplug_open,
    .release = pcie_hotplug_release,
    .unlocked_ioctl = pcie_hotplug_ioctl,
};

static int pcie_hotplug_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int ret;
    char bdf[16];
    struct pcie_hotplug_device *dev;
    struct qdma_dev_conf conf;

    pci_enable_device(pdev);

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
    // printk((KERN_INFO "Probing QDMA for device: %s\n", dev->bdf));
    
    memset(&conf, 0, sizeof(conf));
    conf.qdma_drv_mode = POLL_MODE;
    conf.intr_rngsz = QDMA_INTR_COAL_RING_SIZE;
    conf.pdev = get_next_function_pci_dev(dev->bdf);
    if (!conf.pdev) {
        printk(KERN_ERR "Failed to get next function PCI device for %s\n", dev->bdf);
        ret = -ENODEV;
        goto err_chrdev;
    }
    conf.bar_num_config = -1;
	conf.bar_num_user = -1;
	conf.bar_num_bypass = -1;

	conf.bar_num_config = 2; // @TODO fix //extract_mod_param(pdev, CONFIG_BAR);
	conf.qsets_max = 0;
	conf.qsets_base = -1;
	conf.msix_qvec_max = 32;
	conf.user_msix_qvec_max = 1;
    ret = qdma_device_open("pcie_hotplug", &conf, &dev->dev_hndl);
    if (ret < 0 && ret != -ENODEV) {
        printk(KERN_ERR "Failed to open QDMA device: %d\n", ret);
        goto err_chrdev;
    }
    return 0;

err_chrdev:
    unregister_chrdev_region(dev->devt, 1);
err_bdf:
    kfree(dev->bdf);
    kfree(dev);
    return ret;

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
    pcie_hotplug_class = CLASS_CREATE(CLASS_NAME);
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

    discover_and_add_devices();

    printk(KERN_INFO "PCIe hotplug initialized\n");

    ret = libqdma_init(8, NULL);
    if (ret < 0) {
        printk(KERN_ERR "Failed to initialize libqdma\n");
        pci_unregister_driver(&pcie_hotplug_driver);
        class_destroy(pcie_hotplug_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }
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
    libqdma_exit();
    printk(KERN_INFO "pcie_hotplug module unloaded\n");
}

module_init(pcie_hotplug_init);
module_exit(pcie_hotplug_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("PCIe hotplug module");
MODULE_VERSION("1.0");