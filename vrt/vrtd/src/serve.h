/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
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

#ifndef VRTD_SERVE_H
#define VRTD_SERVE_H

#include <stdbool.h>
#include <stdint.h>

#include <vrtd/wire.h>
#include <systemd/sd-event.h>

#include "array.h"

struct client {
    uint8_t inb[VRTD_MSG_MAX_SIZE];
    uint8_t outb[VRTD_MSG_MAX_SIZE];

    int fd;

    int out_fd;
    bool have_out_fd;

    bool have_request;
    bool have_response;
    bool have_new_response;

    uint32_t wanted_epoll_events;

    uid_t uid;
    struct gid_t_array gids;

    struct vrtd *state;
    struct role *role;

    sd_event_source *event_source;
};

#define CLIENT_IN_HEADER(C) ((struct vrtd_req_header *) (C).inb)
#define CLIENT_OUT_HEADER(C) ((struct vrtd_resp_header *) (C).outb)

#define CLIENT_IN_BODY(C, T) ((struct T *) ((C).inb + sizeof(struct vrtd_req_header)))
#define CLIENT_OUT_BODY(C, T) ((struct T *) ((C).outb + sizeof(struct vrtd_resp_header)))

void cleanup_client(struct client *client);
static inline
void cleanup_clientp(struct client **clientp)
{
    if (clientp == NULL) {
        return;
    }

    cleanup_client(*clientp);

    *clientp = NULL;
}

DECLARE_OWNING_PTR_ARRAY(client_ptr_array, struct client *, cleanup_client)

int on_client_io(sd_event_source *s, int fd, uint32_t revents, void *user);

#endif // VRTD_SERVE_H
