#ifndef VRTD_CTLDEV_H
#define VRTD_CTLDEV_H

#include <stddef.h>

#include <slash_driver/ctldev.h>

#include "array.h"

struct device {
    char *path; /* owning */
    struct slash_ctldev *ctl;
    struct slash_ioctl_bar_info *bar_info[6];
    struct slash_bar_file *bar_files[6];
};

void cleanup_device(struct device *d);
static inline
void cleanup_devicep(struct device **d)
{
    cleanup_device(*d);

    *d = NULL;
}

DECLARE_OWNING_PTR_ARRAY(device_ptr_array, struct device *, cleanup_device);

int devices_discover_and_open(struct device_ptr_array *devices);

#endif // VRTD_CTLDEV_H
