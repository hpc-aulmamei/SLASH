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

#include "vrtd/wire.h"
#define _GNU_SOURCE

#include <slash_driver/uapi/slash_driver_interface.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>

#include <vrtd/vrtd.h>

int vrtd_connect(const char *path)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        return -1;
    }

    struct sockaddr_un sun = {0};
    sun.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sun.sun_path)) {
        errno = ENAMETOOLONG;
        close(fd);
        return -1;
    }
    strcpy(sun.sun_path, path);

    int ret = connect(fd, (struct sockaddr *) &sun, sizeof(sun));
    if (ret == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

enum vrtd_ret vrtd_raw_request(
    int fd,
    uint16_t opcode,
    const void *req_body, uint16_t req_size,
    void *resp_body_buf, size_t resp_bufsz,
    int *out_fd
)
{
    if (req_size > VRTD_MSG_MAX_SIZE - sizeof(struct vrtd_req_header)) { errno = EMSGSIZE; return -1; }

    /* ---- Send ---- */
    struct vrtd_req_header h = {
        .size  = req_size,
        .opcode= opcode,
        .seqno = 1,
    };

    struct iovec siov[2];
    siov[0].iov_base = &h;
    siov[0].iov_len  = sizeof(h);
    siov[1].iov_base = (void*) req_body;
    siov[1].iov_len  = req_size;

    struct msghdr smsg = {
        .msg_iov    = siov,
        .msg_iovlen = req_size ? 2 : 1,
    };

    ssize_t sn = sendmsg(fd, &smsg, MSG_NOSIGNAL);
    if (sn == -1) {
        return VRTD_RET_BAD_CONN;
    }
    if ((size_t) sn != sizeof(h) + req_size) {
        return VRTD_RET_BAD_CONN;
    }

    /* ---- Receive ---- */
    struct vrtd_resp_header rh = {0};

    struct iovec riov[2];
    riov[0].iov_base = &rh;
    riov[0].iov_len  = sizeof(rh);
    riov[1].iov_base = resp_body_buf;
    riov[1].iov_len  = resp_bufsz;

    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr rmsg = {
        .msg_iov        = riov,
        .msg_iovlen     = resp_bufsz ? 2 : 1,
        .msg_control    = out_fd ? cbuf : NULL,
        .msg_controllen = out_fd ? sizeof(cbuf) : 0,
    };

    if (out_fd) {
        *out_fd = -1;
    }

    ssize_t rn = recvmsg(fd, &rmsg, MSG_CMSG_CLOEXEC);
    if (rn == -1) { 
        return VRTD_RET_BAD_CONN;
    }

    if (rmsg.msg_flags & MSG_TRUNC) {
        return VRTD_RET_BAD_LIB_CALL;
    }
    if (rmsg.msg_flags & MSG_CTRUNC) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    if ((size_t)rn < sizeof(rh)) {
        return VRTD_RET_BAD_CONN;
    }

    size_t expect = sizeof(rh) + rh.size;
    if ((size_t) rn != expect) {
        return VRTD_RET_BAD_CONN;
    }

    /* Extract FD if any */
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&rmsg); c != NULL; c = CMSG_NXTHDR(&rmsg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS && c->cmsg_len >= CMSG_LEN(sizeof(int))) {
            assert(out_fd != NULL);
            memcpy(out_fd, CMSG_DATA(c), sizeof(int));
            break;
        }
    }

    return 0;
}

enum vrtd_ret vrtd_get_num_devices(int fd, uint32_t *out)
{
    if (out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_resp_get_num_devices resp = {0};
    int ret = vrtd_raw_request(fd, VRTD_REQ_GET_NUM_DEVICES,
                              NULL, 0,
                              &resp, sizeof(resp),
                              NULL);
    if (ret != VRTD_RET_OK) {
        return ret;
    }

    *out = resp.num_devices;

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_get_device_info(int fd, uint32_t dev, char name_out[128])
{
    if (name_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_req_get_device_info req = {
        .dev_number = dev,
    };
    struct vrtd_resp_get_device_info resp = {0};
    int ret = vrtd_raw_request(fd, VRTD_REQ_GET_DEVICE_INFO,
                              &req, sizeof(req),
                              &resp, sizeof(resp),
                              NULL);
    if (ret != VRTD_RET_OK) {
        return ret;
    }

    strcpy(name_out, resp.name);

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_get_bar_info(int fd, uint32_t dev, uint8_t bar, struct slash_ioctl_bar_info *bar_info_out)
{
    if (bar_info_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_req_get_bar_info req = {
        .dev_number = dev,
        .bar_number = bar,
    };
    struct vrtd_resp_get_bar_info resp = {0};
    int ret = vrtd_raw_request(fd, VRTD_REQ_GET_BAR_INFO,
                              &req, sizeof(req),
                              &resp, sizeof(resp),
                              NULL);
    if (ret != VRTD_RET_OK) {
        return ret;
    }

    memcpy(bar_info_out, &resp.bar_info, sizeof(struct slash_ioctl_bar_info));

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_get_bar_fd(int fd, uint32_t dev, uint8_t bar, int *fd_out, uint64_t *len_out)
{
    if (fd_out == NULL || len_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    struct vrtd_req_get_bar_fd req = {
        .dev_number = dev,
        .bar_number = bar,
    };
    struct vrtd_resp_get_bar_fd resp = {0};
    int ret = vrtd_raw_request(fd, VRTD_REQ_GET_BAR_FD,
                              &req, sizeof(req),
                              &resp, sizeof(resp),
                              fd_out);
    if (ret != VRTD_RET_OK) {
        return ret;
    }

    *len_out = resp.len;

    return VRTD_RET_OK;
}

enum vrtd_ret vrtd_open_bar_file(
    int fd,
    uint32_t dev,
    uint8_t bar,
    struct slash_bar_file *bar_file_out
) {
    if (bar_file_out == NULL) {
        return VRTD_RET_BAD_LIB_CALL;
    }

    int bar_fd = -1;
    size_t len = 0;
    enum vrtd_ret ret = vrtd_get_bar_fd(fd, dev,bar,  &bar_file_out->fd, &bar_file_out->len);
    if (ret != VRTD_RET_OK) {
        return ret;
    }

    bar_file_out->map = mmap(NULL, bar_file_out->len, PROT_READ | PROT_WRITE, MAP_SHARED, bar_file_out->fd, 0);
    if (bar_file_out->map == MAP_FAILED) {
        bar_file_out->map = NULL;
        close(fd);
        return VRTD_RET_INTERNAL_ERROR;
    }

    return VRTD_RET_OK;
}

void vrtd_close_bar_file(struct slash_bar_file *bar_file)
{
    if (bar_file == NULL) {
        return;
    }

    if (bar_file->map != NULL) {
        munmap(bar_file->map, bar_file->len);
        close(bar_file->fd);

        bar_file->map = NULL;
    }
}
