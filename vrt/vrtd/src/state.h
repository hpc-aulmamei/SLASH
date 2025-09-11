#ifndef VRTD_STATE_H
#define VRTD_STATE_H

#include "device.h"
#include "config.h"
#include "serve.h"

struct vrtd {
    struct config *config;

    struct client_ptr_array clients;

    struct device_ptr_array devices;
};

#endif // VRTD_STATE_H
