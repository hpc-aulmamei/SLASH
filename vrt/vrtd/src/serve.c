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
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <stdio.h>
#include <limits.h>
#include <libgen.h>
#include <slash/ctldev.h>
#include <slash/hotplug.h>
#include <slash/qdma.h>

#include "array.h"
#include "auth.h"
#include "clock.h"
#include "design_writer.h"
#include "hotplug.h"
#include "reset.h"
#include "serve.h"
#include "utils.h"
#include "state.h"
#include "vrtd/wire.h"

#define VRTD_DEFERRED_WORK_INTERVAL_USEC (20ULL * 1000ULL)

static int client_update_wanted_epoll_events(struct client *client, sd_event_source *s);
static int client_handle_in(struct client *client);
static int client_handle_out(struct client *client);
static int client_handle_request(struct client *client);
static int client_finalize_pending_design_write(struct client *client);
static uint16_t client_handle_request_get_device_info(
    struct client *client,
    const struct vrtd_req_get_device_info *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_device_info *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_get_device_by_bdf(
    struct client *client,
    const struct vrtd_req_get_device_by_bdf *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_device_by_bdf *resp_body,
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
static uint16_t client_handle_request_buffer_open(
    struct client *client,
    const struct vrtd_req_buffer_open *req_body,
    uint16_t req_size,
    struct vrtd_resp_buffer_open *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
);
static uint16_t client_handle_request_buffer_close(
    struct client *client,
    const struct vrtd_req_buffer_close *req_body,
    uint16_t req_size,
    struct vrtd_resp_buffer_close *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_design_write(
    struct client *client,
    const struct vrtd_req_design_write *req_body,
    uint16_t req_size,
    struct vrtd_resp_design_write *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_device_hotplug_op(
    struct client *client,
    const struct vrtd_req_device_hotplug_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_device_hotplug_op *resp_body,
    uint16_t *resp_size
);
static uint16_t client_handle_request_clock_op(
    struct client *client,
    const struct vrtd_req_clock_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_clock_op *resp_body,
    uint16_t *resp_size
);

static uint16_t device_refresh_pf2_after_design_write(const struct device *d);
static void cleanup_client_buffers(struct client *client);

static const char *vrtd_opcode_to_string(uint16_t opcode)
{
    switch (opcode) {
    case VRTD_REQ_GET_NUM_DEVICES:   return "GET_NUM_DEVICES";
    case VRTD_REQ_GET_DEVICE_INFO:   return "GET_DEVICE_INFO";
    case VRTD_REQ_GET_DEVICE_BY_BDF: return "GET_DEVICE_BY_BDF";
    case VRTD_REQ_GET_BAR_INFO:      return "GET_BAR_INFO";
    case VRTD_REQ_GET_BAR_FD:        return "GET_BAR_FD";
    case VRTD_REQ_QDMA_GET_INFO:     return "QDMA_GET_INFO";
    case VRTD_REQ_QDMA_QPAIR_ADD:   return "QDMA_QPAIR_ADD";
    case VRTD_REQ_QDMA_QPAIR_OP:    return "QDMA_QPAIR_OP";
    case VRTD_REQ_QDMA_QPAIR_GET_FD:return "QDMA_QPAIR_GET_FD";
    case VRTD_REQ_DESIGN_WRITE:      return "DESIGN_WRITE";
    case VRTD_REQ_CLOCK_OP:          return "CLOCK_OP";
    case VRTD_REQ_BUFFER_OPEN:       return "BUFFER_OPEN";
    case VRTD_REQ_BUFFER_CLOSE:      return "BUFFER_CLOSE";
    case VRTD_REQ_DEVICE_HOTPLUG_OP: return "DEVICE_HOTPLUG_OP";
    default:                         return "UNKNOWN";
    }
}

static const char *vrtd_hotplug_op_to_string(uint32_t op)
{
    switch (op) {
    case VRTD_DEVICE_HOTPLUG_OP_RESCAN:         return "rescan";
    case VRTD_DEVICE_HOTPLUG_OP_REMOVE:         return "remove";
    case VRTD_DEVICE_HOTPLUG_OP_TOGGLE_SBR:     return "toggle_sbr";
    case VRTD_DEVICE_HOTPLUG_OP_HOTPLUG:        return "hotplug";
    case VRTD_DEVICE_HOTPLUG_OP_RESET_SEQUENCE: return "reset_sequence";
    default:                                    return "unknown";
    }
}

static uint16_t device_refresh_pf2_after_design_write(const struct device *d)
{
    char pf2_bdf[VRTD_PCI_BDF_LEN] = {0};
    if (pci_bdf_set_function(d->pci_info.bdf, 2, pf2_bdf) != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    struct slash_hotplug *hotplug = slash_hotplug_open(NULL);
    if (hotplug == NULL) {
        return hotplug_errno_to_vrtd_ret(errno);
    }

    int ret = slash_hotplug_remove(hotplug, pf2_bdf);
    if (ret != 0 && errno != ENODEV) {
        int err = errno;
        (void) slash_hotplug_close(hotplug);
        return hotplug_errno_to_vrtd_ret(err);
    }

    if (slash_hotplug_rescan(hotplug) != 0) {
        int err = errno;
        (void) slash_hotplug_close(hotplug);
        return hotplug_errno_to_vrtd_ret(err);
    }

    if (slash_hotplug_close(hotplug) != 0) {
        return VRTD_RET_INTERNAL_ERROR;
    }

    return VRTD_RET_OK;
}

static void cleanup_client_buffers(struct client *client)
{
    if (client == NULL || client->state == NULL || client->conn_id == 0) {
        return;
    }

    LOG(LOG_DEBUG, "Cleaning up buffers for disconnecting client uid=%u conn_id=%llu",
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    for (size_t dev_idx = 0; dev_idx < client->state->devices.len; ++dev_idx) {
        struct device *d = client->state->devices.d[dev_idx];
        if (d == NULL) {
            continue;
        }

        size_t i = 0;
        while (i < d->buffers.len) {
            struct buffer *buf = d->buffers.d[i];
            if (buf == NULL || buf->client_id != client->conn_id) {
                i++;
                continue;
            }

            buffer_ptr_array_rm_by_reference(&d->buffers, buf);
        }
    }
}

void cleanup_client(struct client *client)
{
    if (client == NULL) {
        return;
    }

    cleanup_client_buffers(client);

    gid_t_array_free(&client->gids);

    if (client->in_fd >= 0) {
        (void) close(client->in_fd);
        client->in_fd = -1;
    }

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
        LOG(LOG_DEBUG, "Client disconnected uid=%u conn_id=%llu fd=%d",
            (unsigned int)client->uid, (unsigned long long)client->conn_id, client->fd);
        client_ptr_array_rm_by_reference(&client->state->clients, client);
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

int on_event_deferred_work(sd_event_source *s, uint64_t usec, void *user)
{
    struct vrtd *state = user;

    if (state == NULL) {
        return -1;
    }

    uint64_t next_usec = usec + VRTD_DEFERRED_WORK_INTERVAL_USEC;
    int ret = sd_event_source_set_time(s, next_usec);
    PROPAGATE_ERROR_SD_LOG(ret, LOG_ERR, "Failed to set deferred work timer");

    ret = sd_event_source_set_enabled(s, SD_EVENT_ON);
    PROPAGATE_ERROR_SD_LOG(ret, LOG_ERR, "Failed to re-enable deferred work timer");

    for (size_t i = 0; i < state->clients.len; i++) {
        struct client *client = state->clients.d[i];
        if (client == NULL) {
            continue;
        }

        ret = client_finalize_pending_design_write(client);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to finalize deferred design write");

        if (ret == 1) {
            ret = client_update_wanted_epoll_events(client, client->event_source);
            PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to update epoll events for deferred response");
        }
    }

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

    if (client->in_fd >= 0) {
        (void) close(client->in_fd);
        client->in_fd = -1;
        client->have_in_fd = false;
    }

    struct iovec iovec[1] = {
        { .iov_base = client->inb, .iov_len = VRTD_MSG_MAX_SIZE },
    };

    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_name       = NULL,
        .msg_namelen    = 0,
        .msg_iov        = iovec,
        .msg_iovlen     = SIZEOF_ARRAY(iovec),
        .msg_control    = cbuf,
        .msg_controllen = sizeof(cbuf),
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

    client->in_fd = -1;
    client->have_in_fd = false;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        size_t data_len = cmsg->cmsg_len - CMSG_LEN(0);
        size_t count = data_len / sizeof(int);
        int *fds = (int *) CMSG_DATA(cmsg);
        if (data_len < sizeof(int) || (data_len % sizeof(int)) != 0) {
            for (size_t i = 0; i < count; ++i) {
                (void) close(fds[i]);
            }
            return -1;
        }
        if (count != 1 || client->have_in_fd) {
            for (size_t i = 0; i < count; ++i) {
                (void) close(fds[i]);
            }
            return -1;
        }

        client->in_fd = fds[0];
        client->have_in_fd = true;
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
        LOG(LOG_ERR, "Message truncated");
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

    LOG(LOG_DEBUG, "Request opcode=%u(%s) uid=%u conn_id=%llu",
        (unsigned int)req_header->opcode, vrtd_opcode_to_string(req_header->opcode),
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    // Separate variable for allignment reasons
    uint16_t size = 0;

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
    case VRTD_REQ_GET_DEVICE_BY_BDF:
        resp_header->ret =
            client_handle_request_get_device_by_bdf(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_get_device_by_bdf),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_get_device_by_bdf),
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
    case VRTD_REQ_BUFFER_OPEN:
        resp_header->ret =
            client_handle_request_buffer_open(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_buffer_open),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_buffer_open),
                &size,
                &client->out_fd,
                &client->have_out_fd
            );
        break;
    case VRTD_REQ_BUFFER_CLOSE:
        resp_header->ret =
            client_handle_request_buffer_close(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_buffer_close),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_buffer_close),
                &size
            );
        break;
    case VRTD_REQ_DESIGN_WRITE:
        resp_header->ret =
            client_handle_request_design_write(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_design_write),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_design_write),
                &size
            );
        break;
    case VRTD_REQ_CLOCK_OP:
        resp_header->ret =
            client_handle_request_clock_op(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_clock_op),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_clock_op),
                &size
            );
        break;
    case VRTD_REQ_DEVICE_HOTPLUG_OP:
        resp_header->ret =
            client_handle_request_device_hotplug_op(
                client,
                CLIENT_IN_BODY(*client, vrtd_req_device_hotplug_op),
                req_header->size,
                CLIENT_OUT_BODY(*client, vrtd_resp_device_hotplug_op),
                &size
            );
        break;

    default:
        LOG(LOG_WARNING, "Unknown opcode=%u from uid=%u conn_id=%llu",
            (unsigned int)req_header->opcode,
            (unsigned int)client->uid, (unsigned long long)client->conn_id);
        resp_header->ret = VRTD_RET_BAD_REQUEST;
        resp_header->size = 0;

        break;
    }

    if (client->pending_design_write) {
        return 0;
    }

    if (resp_header->ret != VRTD_RET_OK) {
        LOG(LOG_DEBUG, "Request opcode=%u(%s) failed ret=%u uid=%u conn_id=%llu",
            (unsigned int)req_header->opcode, vrtd_opcode_to_string(req_header->opcode),
            (unsigned int)resp_header->ret,
            (unsigned int)client->uid, (unsigned long long)client->conn_id);
    }

    resp_header->size = size;

    if (client->have_in_fd) {
        (void) close(client->in_fd);
        client->in_fd = -1;
        client->have_in_fd = false;
    }

    client->have_request = false;
    client->have_response = true;
    client->have_new_response = true;

    return 0;
}

static int client_finalize_pending_design_write(struct client *client)
{
    if (client == NULL || !client->pending_design_write) {
        return 0;
    }

    struct device *d = client->pending_design_write_device;
    bool done = false;
    int transfer_error = 0;
    if (d == NULL || d->design_writer == NULL) {
        done = true;
        transfer_error = EIO;
    } else {
        int ret = design_writer_poll_result(d->design_writer, &done, &transfer_error);
        if (ret != 0) {
            done = true;
            transfer_error = (errno != 0) ? errno : EIO;
        }
    }

    if (!done) {
        return 0;
    }

    uint16_t design_write_ret = VRTD_RET_OK;
    if (transfer_error == 0) {
       // design_write_ret = device_refresh_pf2_after_design_write(d);
        LOG(LOG_INFO, "Design write completed successfully for uid=%u conn_id=%llu",
            (unsigned int)client->uid, (unsigned long long)client->conn_id);
    } else {
        LOG(LOG_WARNING, "Design write failed (error=%d) for uid=%u conn_id=%llu",
            transfer_error, (unsigned int)client->uid, (unsigned long long)client->conn_id);
        design_write_ret = VRTD_RET_INTERNAL_ERROR;
    }

    struct vrtd_req_header *req_header = CLIENT_IN_HEADER(*client);
    struct vrtd_resp_header *resp_header = CLIENT_OUT_HEADER(*client);
    struct vrtd_resp_design_write *resp_body = CLIENT_OUT_BODY(*client, vrtd_resp_design_write);

    resp_header->seqno = req_header->seqno;
    resp_header->ret = design_write_ret;

    if (design_write_ret == VRTD_RET_OK) {
        resp_body->zero = 0;
        resp_header->size = sizeof(*resp_body);
    } else {
        resp_header->size = 0;
    }

    client->pending_design_write = false;
    client->pending_design_write_device = NULL;
    client->have_request = false;
    client->have_response = true;
    client->have_new_response = true;

    return 1;
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
        LOG(LOG_WARNING, "get_num_devices: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    resp_body->num_devices = client->state->devices.len;

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "get_num_devices: count=%zu uid=%u conn_id=%llu",
        (size_t)resp_body->num_devices,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    return VRTD_RET_OK;
}

static uint16_t client_handle_request_design_write(
    struct client *client,
    const struct vrtd_req_design_write *req_body,
    uint16_t req_size,
    struct vrtd_resp_design_write *resp_body,
    uint16_t *resp_size
)
{
    (void)resp_body;

    int ret = auth_request_design_write(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "design_write: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "design_write: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->design_writer == NULL) {
        LOG(LOG_NOTICE, "design_write: device %u has no design writer", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    if (!client->have_in_fd || client->in_fd < 0) {
        LOG(LOG_WARNING, "design_write: no input fd provided");
        return VRTD_RET_BAD_REQUEST;
    }

    int fd = client->in_fd;
    bool writer_busy_before = design_writer_is_busy(d->design_writer);
    ret = design_writer_submit_fd_async(d->design_writer, fd);
    if (ret != 0) {
        if (writer_busy_before || design_writer_is_busy(d->design_writer)) {
            LOG(LOG_NOTICE, "design_write: writer busy for device %u", (unsigned int)req_body->dev_number);
            return VRTD_RET_BUSY;
        }
        LOG(LOG_WARNING, "design_write: failed to submit async write for device %u", (unsigned int)req_body->dev_number);
        return VRTD_RET_INTERNAL_ERROR;
    }

    client->in_fd = -1;
    client->have_in_fd = false;

    client->pending_design_write = true;
    client->pending_design_write_device = d;

    LOG(LOG_INFO, "Design write submitted dev=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    *resp_size = 0;
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_device_hotplug_op(
    struct client *client,
    const struct vrtd_req_device_hotplug_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_device_hotplug_op *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_device_hotplug_op(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "hotplug_op: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "hotplug_op: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL) {
        LOG(LOG_NOTICE, "hotplug_op: device %u is null", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    LOG(LOG_INFO, "Hotplug op=%s(%u) bdf=%s dev=%u uid=%u conn_id=%llu",
        vrtd_hotplug_op_to_string(req_body->op), (unsigned int)req_body->op,
        d->pci_info.bdf, (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    switch (req_body->op) {
    case VRTD_DEVICE_HOTPLUG_OP_RESCAN:
        ret = slash_hotplug_rescan(g_hotplug);
        break;
    case VRTD_DEVICE_HOTPLUG_OP_REMOVE:
        ret = slash_hotplug_remove(g_hotplug, d->pci_info.bdf);
        break;
    case VRTD_DEVICE_HOTPLUG_OP_TOGGLE_SBR:
        ret = slash_hotplug_toggle_sbr(g_hotplug, d->pci_info.bdf);
        break;
    case VRTD_DEVICE_HOTPLUG_OP_HOTPLUG:
        ret = slash_hotplug_hotplug(g_hotplug, d->pci_info.bdf);
        break;
    case VRTD_DEVICE_HOTPLUG_OP_RESET_SEQUENCE: {
        uint16_t reset_ret = reset_with_ami(d, &client->state->devices);
        if (reset_ret != VRTD_RET_OK) {
            return reset_ret;
        }
        resp_body->zero = 0;
        *resp_size = sizeof(*resp_body);
        return VRTD_RET_OK;
    }
    default:
        LOG(LOG_WARNING, "hotplug_op: invalid op %u for device %u",
            (unsigned int)req_body->op, (unsigned int)req_body->dev_number);
        return VRTD_RET_INVALID_ARGUMENT;
    }

    if (ret != 0) {
        LOG(LOG_WARNING, "hotplug_op: %s failed for device %u bdf=%s: %m",
            vrtd_hotplug_op_to_string(req_body->op),
            (unsigned int)req_body->dev_number, d->pci_info.bdf);
        return hotplug_errno_to_vrtd_ret(errno);
    }

    resp_body->zero = 0;
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
        LOG(LOG_WARNING, "qdma_get_info: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "qdma_get_info: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        LOG(LOG_NOTICE, "qdma_get_info: device %u has no QDMA", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    if (slash_qdma_info_read(d->qdma, &resp_body->info) != 0) {
        LOG(LOG_WARNING, "qdma_get_info: failed to read info for device %u: %m", (unsigned int)req_body->dev_number);
        return VRTD_RET_INTERNAL_ERROR;
    }

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "qdma_get_info: dev=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

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
        LOG(LOG_WARNING, "qdma_qpair_add: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "qdma_qpair_add: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        LOG(LOG_NOTICE, "qdma_qpair_add: device %u has no QDMA", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    resp_body->add = req_body->add;

    if (slash_qdma_qpair_add(d->qdma, &resp_body->add) != 0) {
        LOG(LOG_WARNING, "qdma_qpair_add: failed for device %u: %m", (unsigned int)req_body->dev_number);
        return VRTD_RET_INTERNAL_ERROR;
    }

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "qdma_qpair_add: dev=%u qid=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number, (unsigned int)resp_body->add.qid,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

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
        LOG(LOG_WARNING, "qdma_qpair_op: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "qdma_qpair_op: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        LOG(LOG_NOTICE, "qdma_qpair_op: device %u has no QDMA", (unsigned int)req_body->dev_number);
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
        LOG(LOG_WARNING, "qdma_qpair_op: invalid op %u for device %u qid=%u",
            (unsigned int)req_body->op, (unsigned int)req_body->dev_number, (unsigned int)req_body->qid);
        return VRTD_RET_INVALID_ARGUMENT;
    }

    if (ret != 0) {
        LOG(LOG_WARNING, "qdma_qpair_op: op %u failed for device %u qid=%u: %m",
            (unsigned int)req_body->op, (unsigned int)req_body->dev_number, (unsigned int)req_body->qid);
        return VRTD_RET_INTERNAL_ERROR;
    }

    resp_body->zero = 0;
    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "qdma_qpair_op: dev=%u qid=%u op=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number, (unsigned int)req_body->qid,
        (unsigned int)req_body->op,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

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
        LOG(LOG_WARNING, "qdma_qpair_get_fd: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "qdma_qpair_get_fd: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL) {
        LOG(LOG_NOTICE, "qdma_qpair_get_fd: device %u has no QDMA", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    int fd = slash_qdma_qpair_get_fd(d->qdma, req_body->qid, (int)req_body->flags);
    if (fd < 0) {
        LOG(LOG_WARNING, "qdma_qpair_get_fd: failed for device %u qid=%u: %m",
            (unsigned int)req_body->dev_number, (unsigned int)req_body->qid);
        return VRTD_RET_INTERNAL_ERROR;
    }

    *out_fd = fd;
    *have_out_fd = true;

    resp_body->zero = 0;
    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "qdma_qpair_get_fd: dev=%u qid=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number, (unsigned int)req_body->qid,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    return VRTD_RET_OK;
}

static uint16_t client_handle_request_buffer_open(
    struct client *client,
    const struct vrtd_req_buffer_open *req_body,
    uint16_t req_size,
    struct vrtd_resp_buffer_open *resp_body,
    uint16_t *resp_size,
    int *out_fd,
    bool *have_out_fd
)
{
    int ret = auth_request_buffer_open(client, req_body);
    if (ret == -1) {
        char pwbuf[1024];
        LOG(LOG_WARNING, "Failed to authorize buffer open request for uid %u(%s): %m",
            (unsigned int) client->uid, uid_to_username(client->uid, pwbuf, sizeof(pwbuf)));
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;
    *have_out_fd = false;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "Received malformed buffer open request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_WARNING, "Received buffer open request for non-existent device");
        return VRTD_RET_NOEXIST;
    }

    if (req_body->size == 0) {
        LOG(LOG_WARNING, "Received buffer open request with zero size");
        return VRTD_RET_INVALID_ARGUMENT;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->qdma == NULL || d->memory_map == NULL) {
        LOG(LOG_WARNING, "Received buffer open request for non-existent or non-functional device");
        return VRTD_RET_NOEXIST;
    }

    uint64_t client_id = client->conn_id;
    if (client_id == 0) {
        LOG(LOG_ERR, "Invalid client connection id");
        return VRTD_RET_INTERNAL_ERROR;
    }

    _cleanup_(cleanup_bufferp)
    struct buffer *buf = buffer_create(
        d->qdma,
        d->memory_map,
        (enum allocation_type) req_body->alloc_type,
        (enum vrtd_alloc_dir) req_body->alloc_dir,
        req_body->size,
        req_body->alloc_arg,
        client_id,
        NULL
    );
    if (buf == NULL) {
        if (errno == EINVAL) {
            LOG(LOG_WARNING, "buffer_open: invalid allocation arguments for device %u", (unsigned int)req_body->dev_number);
            return VRTD_RET_INVALID_ARGUMENT;
        }
        if (errno == ENOMEM) {
            LOG(LOG_NOTICE, "buffer_open: out of memory for device %u size=%llu",
                (unsigned int)req_body->dev_number, (unsigned long long)req_body->size);
            return VRTD_RET_BUSY;
        }

        LOG(LOG_ERR, "Failed to create buffer for buffer open request: %m");
        return VRTD_RET_INTERNAL_ERROR;
    }

    if (buf->fd < 0) {
        LOG(LOG_ERR, "Buffer created without valid fd");
        return VRTD_RET_INTERNAL_ERROR;
    }

    uint64_t real_size = buf->size;
    int fd = buf->fd;
    uint64_t phys_addr = buf->addr;

    if (buffer_ptr_array_push_move(&d->buffers, &buf) != 0) {
        LOG(LOG_ERR, "Failed to add buffer to device buffer list");
        return VRTD_RET_INTERNAL_ERROR;
    }

    resp_body->size = real_size;
    resp_body->phys_addr = phys_addr;
    *out_fd = fd;
    *have_out_fd = true;
    *resp_size = sizeof(*resp_body);

    LOG(LOG_INFO, "Buffer opened size=%llu phys_addr=0x%llx dev=%u uid=%u conn_id=%llu",
        (unsigned long long)real_size, (unsigned long long)phys_addr,
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    return VRTD_RET_OK;
}

static uint16_t client_handle_request_buffer_close(
    struct client *client,
    const struct vrtd_req_buffer_close *req_body,
    uint16_t req_size,
    struct vrtd_resp_buffer_close *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_buffer_close(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "buffer_close: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "buffer_close: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    if (req_body->size == 0) {
        LOG(LOG_WARNING, "buffer_close: zero size");
        return VRTD_RET_INVALID_ARGUMENT;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL) {
        LOG(LOG_NOTICE, "buffer_close: device %u is null", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct buffer *found = NULL;
    for (size_t i = 0; i < d->buffers.len; ++i) {
        struct buffer *buf = d->buffers.d[i];
        if (buf == NULL) {
            continue;
        }
        if (buf->addr != req_body->phys_addr) {
            continue;
        }
        if (buf->size != req_body->size) {
            LOG(LOG_WARNING, "buffer_close: size mismatch at addr=0x%llx (expected %llu, got %llu)",
                (unsigned long long)req_body->phys_addr,
                (unsigned long long)buf->size, (unsigned long long)req_body->size);
            return VRTD_RET_INVALID_ARGUMENT;
        }
        if (buf->client_id != client->conn_id) {
            char pwbuf[1024];
            LOG(
                LOG_WARNING,
                "Permission denied for uid %u(%s): 'buffer_close' requires buffer ownership",
                (unsigned int) client->uid,
                uid_to_username(client->uid, pwbuf, sizeof(pwbuf))
            );
            return VRTD_RET_AUTH_ERROR;
        }
        found = buf;
        break;
    }

    if (found == NULL) {
        LOG(LOG_NOTICE, "buffer_close: no buffer at addr=0x%llx on device %u",
            (unsigned long long)req_body->phys_addr, (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    LOG(LOG_INFO, "Buffer closed addr=0x%llx size=%llu dev=%u uid=%u conn_id=%llu",
        (unsigned long long)found->addr, (unsigned long long)found->size,
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    buffer_ptr_array_rm_by_reference(&d->buffers, found);

    resp_body->zero = 0;
    *resp_size = sizeof(*resp_body);
    return VRTD_RET_OK;
}

static uint16_t client_handle_request_clock_op(
    struct client *client,
    const struct vrtd_req_clock_op *req_body,
    uint16_t req_size,
    struct vrtd_resp_clock_op *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_clock_op(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "clock_op: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "clock_op: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];
    if (d == NULL || d->clock_driver == NULL) {
        LOG(LOG_NOTICE, "clock_op: device %u has no clock driver", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    uint32_t rate = req_body->rate_hz;

    switch (req_body->region) {
    case VRTD_CLOCK_REGION_SERVICE:
        if (req_body->op == VRTD_CLOCK_OP_GET) {
            if (clock_driver_get_service_region_rate_hz(d->clock_driver, &rate) != 0) {
                LOG(LOG_WARNING, "clock_op: failed to get service region rate for device %u: %m",
                    (unsigned int)req_body->dev_number);
                return VRTD_RET_INTERNAL_ERROR;
            }
        } else if (req_body->op == VRTD_CLOCK_OP_SET) {
            if (rate == 0) {
                LOG(
                    LOG_WARNING,
                    "Received set frequency request with zero rate for service region"
                );
                return VRTD_RET_INVALID_ARGUMENT;
            }
            if (clock_driver_set_service_region_rate_hz(d->clock_driver, &rate) != 0) {
                LOG(
                    LOG_ERR,
                    "Failed to set service region frequency to %u Hz: %m",
                    req_body->rate_hz
                );
                return VRTD_RET_INTERNAL_ERROR;
            }
        } else {
            LOG(
                LOG_WARNING,
                "Received invalid clock op %u for service region",
                (unsigned int)req_body->op
            );
            return VRTD_RET_INVALID_ARGUMENT;
        }
        break;
    case VRTD_CLOCK_REGION_USER:
        if (req_body->op == VRTD_CLOCK_OP_GET) {
            if (clock_driver_get_user_region_rate_hz(d->clock_driver, &rate) != 0) {
                LOG(LOG_WARNING, "clock_op: failed to get user region rate for device %u: %m",
                    (unsigned int)req_body->dev_number);
                return VRTD_RET_INTERNAL_ERROR;
            }
        } else if (req_body->op == VRTD_CLOCK_OP_SET) {
            if (rate == 0) {
                LOG(
                    LOG_WARNING,
                    "Received set frequency request with zero rate for user region"
                );
                return VRTD_RET_INVALID_ARGUMENT;
            }
            if (clock_driver_set_user_region_rate_hz(d->clock_driver, &rate) != 0) {
                LOG(
                    LOG_ERR,
                    "Failed to set user region frequency to %u Hz: %m",
                    req_body->rate_hz
                );
                return VRTD_RET_INTERNAL_ERROR;
            }
        } else {
            LOG(
                LOG_WARNING,
                "Received invalid clock op %u for user region",
                (unsigned int)req_body->op
            );
            return VRTD_RET_INVALID_ARGUMENT;
        }
        break;
    default:
        LOG(
            LOG_WARNING,
            "Received clock request with invalid region %u",
            (unsigned int)req_body->region
        );
        return VRTD_RET_INVALID_ARGUMENT;
    }

    resp_body->rate_hz = rate;
    *resp_size = sizeof(*resp_body);

    LOG(LOG_INFO, "clock_op: op=%u region=%u rate_hz=%u dev=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->op, (unsigned int)req_body->region, rate,
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

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
        LOG(LOG_WARNING, "get_device_info: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "get_device_info: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    struct device *d = client->state->devices.d[req_body->dev_number];

    _cleanup_(cleanup_free)
    char *path = strdup(d->path);
    if (unlikely(path == NULL)) {
        LOG(LOG_WARNING, "get_device_info: allocation failure");
        return VRTD_RET_INTERNAL_ERROR;
    }

    memset(resp_body, 0, sizeof(*resp_body));
    snprintf(resp_body->info.name, sizeof(resp_body->info.name), "%s", basename(path));
    memcpy(&resp_body->info.pci, &d->pci_info, sizeof(struct vrtd_pci_info));

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "get_device_info: dev=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    return VRTD_RET_OK;
}

static uint16_t client_handle_request_get_device_by_bdf(
    struct client *client,
    const struct vrtd_req_get_device_by_bdf *req_body,
    uint16_t req_size,
    struct vrtd_resp_get_device_by_bdf *resp_body,
    uint16_t *resp_size
)
{
    int ret = auth_request_get_device_by_bdf(client, req_body);
    if (ret == -1) {
        return VRTD_RET_INTERNAL_ERROR;
    } else if (ret == 0) {
        return VRTD_RET_AUTH_ERROR;
    }

    *resp_size = 0;

    if (req_size < sizeof(*req_body)) {
        LOG(LOG_WARNING, "get_device_by_bdf: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    char bdf[VRTD_PCI_BDF_LEN];
    memcpy(bdf, req_body->bdf, sizeof(bdf));
    bdf[sizeof(bdf) - 1] = '\0';

    if (bdf[0] == '\0') {
        LOG(LOG_WARNING, "get_device_by_bdf: empty BDF string");
        return VRTD_RET_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < client->state->devices.len; ++i) {
        struct device *d = client->state->devices.d[i];

        if (strcmp(d->pci_info.bdf, bdf) == 0) {
            resp_body->dev_number = (uint32_t) i;
            *resp_size = sizeof(*resp_body);
            LOG(LOG_DEBUG, "get_device_by_bdf: bdf=%s -> dev=%u uid=%u conn_id=%llu",
                bdf, (unsigned int)i,
                (unsigned int)client->uid, (unsigned long long)client->conn_id);
            return VRTD_RET_OK;
        }
    }

    LOG(LOG_NOTICE, "get_device_by_bdf: no device found for bdf=%s", bdf);
    return VRTD_RET_NOEXIST;
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
        LOG(LOG_WARNING, "get_bar_info: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "get_bar_info: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    if (req_body->bar_number >= 6) {
        LOG(LOG_WARNING, "get_bar_info: invalid BAR number %u", (unsigned int)req_body->bar_number);
        return VRTD_RET_BAD_REQUEST;
    }

    // TODO: Free this
    struct slash_ioctl_bar_info *bar_info = client->state->devices.d[req_body->dev_number]->bar_info[req_body->bar_number];
    if (bar_info == NULL) {
        LOG(LOG_NOTICE, "get_bar_info: BAR %u not available on device %u",
            (unsigned int)req_body->bar_number, (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    resp_body->bar_info = *bar_info;

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "get_bar_info: dev=%u bar=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number, (unsigned int)req_body->bar_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

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
        LOG(LOG_WARNING, "get_bar_fd: malformed request");
        return VRTD_RET_BAD_REQUEST;
    }

    if (req_body->dev_number >= client->state->devices.len) {
        LOG(LOG_NOTICE, "get_bar_fd: device %u does not exist", (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    if (req_body->bar_number >= 6) {
        LOG(LOG_WARNING, "get_bar_fd: invalid BAR number %u", (unsigned int)req_body->bar_number);
        return VRTD_RET_BAD_REQUEST;
    }

    struct slash_bar_file *bar_file = client->state->devices.d[req_body->dev_number]->bar_files[req_body->bar_number];
    if (bar_file == NULL) {
        LOG(LOG_NOTICE, "get_bar_fd: BAR %u not available on device %u",
            (unsigned int)req_body->bar_number, (unsigned int)req_body->dev_number);
        return VRTD_RET_NOEXIST;
    }

    resp_body->len = bar_file->len;
    *out_fd = bar_file->fd;
    *have_out_fd = true;

    *resp_size = sizeof(*resp_body);

    LOG(LOG_DEBUG, "get_bar_fd: dev=%u bar=%u uid=%u conn_id=%llu",
        (unsigned int)req_body->dev_number, (unsigned int)req_body->bar_number,
        (unsigned int)client->uid, (unsigned long long)client->conn_id);

    return VRTD_RET_OK;
}
