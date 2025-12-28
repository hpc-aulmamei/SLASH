#ifndef PCIE_HOTPLUG_IOCTL_H
#define PCIE_HOTPLUG_IOCTL_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/ioctl.h>


#define PCIE_IOCTL_MAGIC 0xB5

#define PCIE_IOCTL_RESCAN      _IO(PCIE_IOCTL_MAGIC, 0x01)
#define PCIE_IOCTL_REMOVE      _IO(PCIE_IOCTL_MAGIC, 0x02)
#define PCIE_IOCTL_TOGGLE_SBR  _IO(PCIE_IOCTL_MAGIC, 0x03)
#define PCIE_IOCTL_HOTPLUG     _IO(PCIE_IOCTL_MAGIC, 0x04)

struct pcie_bar_read {
    unsigned int bar_index;       // IN: BAR number (0-5)
    unsigned int offset;          // IN: Offset in BAR to read
    u32 value;                    // OUT: Value read (32-bit)
};
#define PCIE_IOCTL_GET_BAR_VAL _IOWR(PCIE_IOCTL_MAGIC, 0x05, struct pcie_bar_read)

struct pcie_bar_write {
    uint8_t bar_index;
    uint32_t offset;
    uint32_t value;
};

#define PCIE_IOCTL_SET_BAR_VAL _IOWR(PCIE_IOCTL_MAGIC, 0x06, struct pcie_bar_write)


static int major_number;
static struct class* pcie_hotplug_class = NULL;

struct pcie_hotplug_device {
    char *bdf;
    char rootport_bdf[32];
    dev_t devt;
    struct cdev cdev;
    struct list_head list;
    unsigned long dev_hndl; // Handle for libqdma
};

#define MAX_BAR_RW_SIZE 128  // Adjust as needed

struct pcie_bar_range {
    uint8_t bar_index;
    uint32_t offset;
    uint32_t size;
    uint8_t data[MAX_BAR_RW_SIZE];  // For both read & write
};

#define PCIE_IOCTL_READ_BAR_RANGE  _IOWR(PCIE_IOCTL_MAGIC, 0x07, struct pcie_bar_range)
#define PCIE_IOCTL_WRITE_BAR_RANGE _IOWR(PCIE_IOCTL_MAGIC, 0x08, struct pcie_bar_range)


static long pcie_hotplug_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif // PCIE_HOTPLUG_IOCTL_H