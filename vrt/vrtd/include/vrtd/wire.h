#ifndef VRTD_WIRE_H
#define VRTD_WIRE_H

#include <stdint.h>

#include <slash_driver/uapi/slash_driver_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRTD_MSG_MAX_SIZE 4096

enum vrtd_opcode {
    VRTD_REQ_GET_NUM_DEVICES,
    VRTD_REQ_GET_DEVICE_INFO,
    VRTD_REQ_GET_BAR_INFO,
    VRTD_REQ_GET_BAR_FD,
};

enum vrtd_ret {
    VRTD_RET_OK,
    VRTD_RET_BAD_LIB_CALL, ///< Bad library call to libvrtd. This code will not be returned on the wire.
    VRTD_RET_BAD_CONN, ///< libvrtd could not connect to vrtd. This code will not be returned on the wire.
    VRTD_RET_BAD_REQUEST,
    VRTD_RET_INVALID_ARGUMENT,
    VRTD_RET_NOEXIST,
    VRTD_RET_INTERNAL_ERROR,
    VRTD_RET_AUTH_ERROR,
};

struct vrtd_req_header {
    uint16_t size;
    uint16_t opcode;
    uint32_t seqno;
} __attribute__((packed));

struct vrtd_resp_header {
    uint16_t size;
    uint16_t ret;
    uint32_t seqno;
} __attribute__((packed));

struct vrtd_req_get_num_devices {
} __attribute__((packed));

struct vrtd_resp_get_num_devices {
    uint32_t num_devices;
} __attribute__((packed));

struct vrtd_req_get_device_info {
    uint32_t dev_number;
} __attribute__((packed));

struct vrtd_resp_get_device_info {
    char name[128];
} __attribute__((packed));

struct vrtd_req_get_bar_info {
    uint32_t dev_number;
    uint8_t bar_number;
} __attribute__((packed));

struct vrtd_resp_get_bar_info {
    struct slash_ioctl_bar_info bar_info;
} __attribute__((packed));

struct vrtd_req_get_bar_fd {
    uint32_t dev_number;
    uint8_t bar_number;
} __attribute__((packed));

struct vrtd_resp_get_bar_fd {
    uint64_t len;

    /* fd out of band */
} __attribute__((packed));

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VRTD_WIRE_H
