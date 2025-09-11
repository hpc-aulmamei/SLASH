#ifndef VRTD_AUTH_H
#define VRTD_AUTH_H

#include "serve.h"

int auth_request_get_device_info(
    struct client *client,
    const struct vrtd_req_get_device_info *req_body
);

int auth_request_get_num_devices(
    struct client *client,
    const struct vrtd_req_get_num_devices *req_body
);

int auth_request_get_bar_info(
    struct client *client,
    const struct vrtd_req_get_bar_info *req_body
);

int auth_request_get_bar_fd(
    struct client *client,
    const struct vrtd_req_get_bar_fd *req_body
);

#endif // VRTD_AUTH_H
