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

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <stdio.h>
#include <slash/qdma.h>

#include "array.h"
#include "auth.h"
#include "serve.h"
#include "utils.h"
#include "state.h"
#include "vrtd/wire.h"

static int client_update_wanted_epoll_events(struct client *client, sd_event_source *s);
static int client_handle_in(struct client *client);
static int client_handle_out(struct client *client);
static int client_handle_request(struct client *client);
static uint16_t client_handle_request_get_device_info(
    struct client *client,
    const struct vrtd_req_get_device_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_device_info *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_get_num_devices(
    struct client *client,
    const struct vrtd_req_get_num_devices *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_num_devices *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_get_bar_info(
    struct client *client,
    const struct vrtd_req_get_bar_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_bar_info *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_get_bar_fd(
    struct client *client,
    const struct vrtd_req_get_bar_fd *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_bar_fd *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
);
static uint16_t client_handle_request_qdma_get_info(
    struct client *client,
    const struct vrtd_req_qdma_get_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_get_info *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_qdma_qpair_add(
    struct client *client,
    const struct vrtd_req_qdma_qpair_add *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_add *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_qdma_qpair_op(
    struct client *client,
    const struct vrtd_req_qdma_qpair_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_op *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_qdma_qpair_get_fd(
    struct client *client,
    const struct vrtd_req_qdma_qpair_get_fd *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_get_fd *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
);

void cleanup_client(struct client *client)
{
    if (client == NULL) {
        return;
    }

    gid_t_array_free(&client->gids);

    if (client->fd >= 0) {
        (void) close(client->fd);
        client->fd = -1;
    }

    (void) sd_event_source_disable_unrefp(&client->event_source);

    free(client);
}

int on_client_io(sd_event_source *s, int fd, uint32_t revents, void *user)
{
    struct client *client = user;
    (void) s;

    assert(client->fd == fd);

    int ret;

    if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        client_ptr_array_rm_by_value(&client->state->clients, client);
        return 0;
    }

    if (!client->have_request && (revents & EPOLLIN)) {
        ret = client_handle_in(client);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to handle client input");
    }

    if (client->have_request && !client->have_response) {
        ret = client_handle_request(client);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to handle client request");
    }

    if ((client->have_response && (revents & EPOLLOUT)) ||
         client->have_new_response) {
        client->have_new_response = false;

        ret = client_handle_out(client);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to handle client output");
    }

    ret = client_update_wanted_epoll_events(client, s);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to update epoll events");



    return 0;
}

static int client_update_wanted_epoll_events(struct client *client, sd_event_source *s)
{
    uint32_t events =
        EPOLLRDHUP | 
        (!client->have_request ? EPOLLIN : 0) |
        (client->have_response ? EPOLLOUT : 0)
    ;

    if (events == client->wanted_epoll_events) {
        return 0;
    }
    client->wanted_epoll_events = events;

    int ret = sd_event_source_set_io_events(s, events);
    PROPAGATE_ERROR_SD_LOG(ret, LOG_ERR, "Failed to set io source io events");

    return 0;
}

static int client_handle_in(struct client *client)
{
    assert(!client->have_request);

    struct iovec iovec[1] = {
        { .iov_base = client->inb, .iov_len = VRTD_MSG_MAX_SIZE },
    };

    struct msghdr msg = {
        .msg_name       = NULL,
        .msg_namelen    = 0,
        .msg_iov        = iovec,
        .msg_iovlen     = SIZEOF_ARRAY(iovec),
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0,
    };

    ssize_t n;
retry:
    n = recvmsg(client->fd, &msg, MSG_DONTWAIT);
    if (n == -1) {
        switch (errno) {
        case EINTR:
            goto retry;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            return 0;
        default:
            return -1;
        }
    }

    if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
        // TODO: handle error from client
        return -1;
    }

    struct vrtd_req_header *header = (struct vrtd_req_header *) client->inb;
    if (n < sizeof(struct vrtd_req_header) || header->size + sizeof(struct vrtd_req_header) != n || header->size > VRTD_MSG_MAX_SIZE - sizeof *header) {
        // TODO: handle error from client
        return -1;
    }

    client->have_request = true;

    return 0;
}

static int client_handle_out(struct client *client)
{
    assert(client->have_response);

    size_t size = sizeof(struct vrtd_resp_header) + ((struct vrtd_resp_header *) client->outb)->size;

    struct iovec iovec[1] = {
        { .iov_base = client->outb, .iov_len = size },
    };

    struct msghdr msg = {
        .msg_name       = NULL,
        .msg_namelen    = 0,
        .msg_iov        = iovec,
        .msg_iovlen     = SIZEOF_ARRAY(iovec),
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0,
    };

    char cbuf[CMSG_SPACE(sizeof(int))];

    if (client->have_out_fd) {
        memset(cbuf, 0, sizeof cbuf);

        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof cbuf;

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN(sizeof(int));

        memcpy(CMSG_DATA(cmsg), &client->out_fd, sizeof(int));
    }

    ssize_t n;
retry:
    n = sendmsg(client->fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n == -1) {
        switch (errno) {
        case EINTR:
            goto retry;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            return 0;
        default:
            return -1;
        }
    }

    if (n != size) {
        (void) sd_journal_print(LOG_ERR, "Message truncated");
        return -1;
    }

    client->have_response = false;
    client->have_out_fd = false;

    return 0;
}

static int client_handle_request(struct client *client)
{
    assert(client->have_request);
    assert(!client->have_response);

    struct vrtd_req_header *req_header = CLIENT_IN_HEADER(*client);
    struct vrtd_resp_header *resp_header = CLIENT_OUT_HEADER(*client);

    resp_header->seqno = req_header->seqno;

    // Separate variable for allignment reasons
    uint16_t size;

    switch (req_header->opcode) {
    case VRTD_REQ_GET_NUM_DEVICES:
        resp_header->ret =
            client_handle_request_get_num_devices(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_get_num_devices),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_get_num_devices),
                &size
            );
        break;
    case VRTD_REQ_GET_DEVICE_INFO:
        resp_header->ret =
            client_handle_request_get_device_info(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_get_device_info),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_get_device_info),
                &size
            );
        break;
    case VRTD_REQ_GET_BAR_INFO:
        resp_header->ret =
            client_handle_request_get_bar_info(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_get_bar_info),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_get_bar_info),
                &size
            );
        break;
    case VRTD_REQ_GET_BAR_FD:
        resp_header->ret =
            client_handle_request_get_bar_fd(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_get_bar_fd),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_get_bar_fd),
                &size,
                &client->out_fd,
                &client->have_out_fd
            );
        break;
    case VRTD_REQ_QDMA_GET_INFO:
        resp_header->ret =
            client_handle_request_qdma_get_info(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_qdma_get_info),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_qdma_get_info),
                &size
            );
        break;
    case VRTD_REQ_QDMA_QPAIR_ADD:
        resp_header->ret =
            client_handle_request_qdma_qpair_add(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_qdma_qpair_add),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_qdma_qpair_add),
                &size
            );
        break;
    case VRTD_REQ_QDMA_QPAIR_OP:
        resp_header->ret =
            client_handle_request_qdma_qpair_op(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_qdma_qpair_op),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_qdma_qpair_op),
                &size
            );
        break;
    case VRTD_REQ_QDMA_QPAIR_GET_FD:
        resp_header->ret =
            client_handle_request_qdma_qpair_get_fd(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_qdma_qpair_get_fd),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_qdma_qpair_get_fd),
                &size,
                &client->out_fd,
                &client->have_out_fd
            );
        break;

    default:
        resp_header->ret = VRTD_RET_BAD_REQUEST;
        resp_header->size = 0;

        break;
    }

    resp_header->size = size;

    client->have_request = false;
    client->have_response = true;
    client->have_new_response = true;

    return 0;
}

static uint16_t client_handle_request_get_num_devices(
    struct client *client,
    const struct vrtd_req_get_num_devices *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_num_devices *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_get_num_devices(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    resp_body->num_devices = client->state->devices.len;
    
    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_qdma_get_info(
    struct client *client,
    const struct vrtd_req_qdma_get_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_get_info *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_qdma_get_info(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        return VRTD_RET_NOEXIST;
    }

    if (slash_qdma_info_read(d->qdma, &resp_body->info) != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_qdma_qpair_add(
    struct client *client,
    const struct vrtd_req_qdma_qpair_add *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_add *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_qdma_qpair_add(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        return VRTD_RET_NOEXIST;
    }

    resp_body->add = req_body->add;

    if (slash_qdma_qpair_add(d->qdma, &resp_body->add) != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_qdma_qpair_op(
    struct client *client,
    const struct vrtd_req_qdma_qpair_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_op *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_qdma_qpair_op(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        return VRTD_RET_NOEXIST;
    }

    switch (req_body->op) {
    case SLASH_QDMA_QUEUE_OP_START:
        ret = slash_qdma_qpair_start(d->qdma, req_body->qid);
        break;
    case SLASH_QDMA_QUEUE_OP_STOP:
        ret = slash_qdma_qpair_stop(d->qdma, req_body->qid);
        break;
    case SLASH_QDMA_QUEUE_OP_DEL:
        ret = slash_qdma_qpair_del(d->qdma, req_body->qid);
        break;
    default:
        return VRTD_RET_INVALID_ARGUMENT;
    }

    if (ret != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    resp_body->zero = 0;
    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_qdma_qpair_get_fd(
    struct client *client,
    const struct vrtd_req_qdma_qpair_get_fd *req_body,
    uint16_t req_size,
    struct vrtd_resp_qdma_qpair_get_fd *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
)
{
    int ret = auth_request_qdma_qpair_get_fd(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;
    *have_out_fd = false;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        return VRTD_RET_NOEXIST;
    }

    int fd = slash_qdma_qpair_get_fd(d->qdma, req_body->qid, (int)req_body->flags);
    if (fd < 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    *out_fd = fd;
    *have_out_fd = true;

    resp_body->zero = 0;
    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_get_device_info(
    struct client *client,
    const struct vrtd_req_get_device_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_device_info *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_get_device_info(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    _cleanup_(cleanup_free)
    char *path = strdup(client->state->devices.d[req_body->dev_number]->path);
    if (unlikely(path == NULL)) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    snprintf(resp_body->name, sizeof resp_body->name, "%s", basename(path));

    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_get_bar_info(
    struct client *client,
    const struct vrtd_req_get_bar_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_bar_info *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_get_bar_info(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    if (req_body->bar_number >= 6) {
        return VRTD_RET_BAD_REQUEST;
    }

    // TODO: Free this
    struct slash_ioctl_bar_info *bar_info = client->state->devices.d[req_body->dev_number]->bar_info[req_body->bar_number];
    if (bar_info == NULL) {
        return VRTD_RET_NOEXIST;
    }

    resp_body->bar_info = *bar_info;

    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_get_bar_fd(
    struct client *client,
    const struct vrtd_req_get_bar_fd *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_bar_fd *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
)
{
    int ret = auth_request_get_bar_fd(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;
    *have_out_fd = false;

    if (req_size < sizeof(*req_body)) {
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        return VRTD_RET_NOEXIST;
    }

    if (req_body->bar_number >= 6) {
        return VRTD_RET_BAD_REQUEST;
    }

    struct slash_bar_file *bar_file = client->state->devices.d[req_body->dev_number]->bar_files[req_body->bar_number];
    if (bar_file == NULL) {
        return VRTD_RET_NOEXIST;
    }

    resp_body->len = bar_file->len;
    *out_fd = bar_file->fd;
    *have_out_fd = true;

    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}
