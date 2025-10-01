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

/**
 * @file wire.h
 * @brief On‑wire protocol for vrtd (V80 Runtime Daemon).
 *
 * Transport:
 *  - UNIX domain sockets (AF_UNIX, SOCK_SEQPACKET). Messages are record‑oriented.
 *  - File descriptors may be sent out‑of‑band using SCM_RIGHTS.
 *
 * Framing:
 *  - Each message = { header, body }.
 *  - Total size (header + body) MUST be <= VRTD_MSG_MAX_SIZE.
 *
 * Sequencing:
 *  - Requests carry a client‑chosen @ref vrtd_req_header::seqno that is echoed
 *    unmodified by the server in @ref vrtd_resp_header::seqno.
 *
 * Versioning/Extensibility:
 *  - Unknown opcodes result in VRTD_RET_BAD_REQUEST.
 *  - New fields may be added at the *end* of messages; older peers must ignore
 *    trailing bytes up to @ref vrtd_resp_header::size.
 *
 * Security:
 *  - Server enforces permissions; failures surface as VRTD_RET_AUTH_ERROR.
 */

#ifndef VRTD_WIRE_H
#define VRTD_WIRE_H

#include <stdint.h>

#include <slash/uapi/slash_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum total size (header + body) for any vrtd message in bytes. */
#define VRTD_MSG_MAX_SIZE 4096

/**
 * @brief Operations the client can request from the server.
 * @note Unknown/unsupported opcodes yield VRTD_RET_BAD_REQUEST.
 */
enum vrtd_opcode {
    /** Query the number of SLASH devices. */
    VRTD_REQ_GET_NUM_DEVICES,

    /** Query basic information about a device. */
    VRTD_REQ_GET_DEVICE_INFO,

    /** Query metadata about a device BAR. */
    VRTD_REQ_GET_BAR_INFO,

    /** Obtain a device BAR file descriptor via SCM_RIGHTS. */
    VRTD_REQ_GET_BAR_FD,
};

/**
 * @brief Return codes for vrtd operations.
 *
 * @warning VRTD_RET_BAD_LIB_CALL and VRTD_RET_BAD_CONN are **client‑local**
 *          and are never returned by the server on the wire.
 */

enum vrtd_ret {
    VRTD_RET_OK,
    VRTD_RET_BAD_LIB_CALL, ///< Bad library call to libvrtd. This code will not be returned on the wire.
    VRTD_RET_BAD_CONN, ///< libvrtd could not connect to vrtd. This code will not be returned on the wire.
    VRTD_RET_BAD_REQUEST, ///< Malformed request.
    VRTD_RET_INVALID_ARGUMENT, ///< Invalid argument.
    VRTD_RET_NOEXIST, ///< Requested resource does not exist.
    VRTD_RET_INTERNAL_ERROR, ///< Internal error in the vrtd daemon. Check the vrtd log.
    VRTD_RET_AUTH_ERROR, ///< User does not have permission to execute request.
};

struct vrtd_req_header {
    uint16_t size; ///< Size of the request body (not including the header).
    uint16_t opcode; ///< See @ref vrtd_opcode.
    uint32_t seqno; ///< Sequence number (this will simply be echoed by the server in the response header).
} __attribute__((packed));

struct vrtd_resp_header {
    uint16_t size; ///< Size of the response body (not including the header).
    uint16_t ret; ///< See @ref vrtd_ret.
    uint32_t seqno; ///< Sequence number (this is simply echoed from the request header).
} __attribute__((packed));

/**
 * @brief Placeholder body to avoid empty-struct ABI pitfalls across C/C++.
 * @note Must be set to zero by clients; servers must ignore its value.
 */
struct vrtd_req_get_num_devices {
    uint8_t zero;
} __attribute__((packed));


struct vrtd_resp_get_num_devices {
    uint32_t num_devices; ///< Number of SLASH devices known to the server. They are identified by numbers in the range [0, n).
} __attribute__((packed));


struct vrtd_req_get_device_info {
    uint32_t dev_number; ///< The device for which to get info. An index in the range [0, n).
} __attribute__((packed));

struct vrtd_resp_get_device_info {
    char name[128]; ///< The name of the device.
} __attribute__((packed));

struct vrtd_req_get_bar_info {
    uint32_t dev_number; ///< The device for which to get info. An index in the range [0, n).
    uint8_t bar_number; ///< The BAR for which to get info. An index in the range [0, 6).
} __attribute__((packed));

struct vrtd_resp_get_bar_info {
    struct slash_ioctl_bar_info bar_info; ///< The structure with BAR information.
} __attribute__((packed));

struct vrtd_req_get_bar_fd {
    uint32_t dev_number; ///< The device for who's BAR to get a file descriptor. An index in the range [0, n).
    uint8_t bar_number; ///< The BAR for which to get a file descriptor. An index in the range [0, 6).
} __attribute__((packed));

/**
 * @brief Response to VRTD_REQ_GET_BAR_FD.
 *
 * The BAR file descriptor is sent out-of-band via SCM_RIGHTS in the same
 * message and is present only when @ref vrtd_resp_header::ret == VRTD_RET_OK.
 */
struct vrtd_resp_get_bar_fd {
    uint64_t len; ///< Size of the BAR address space; suitable for mmap.
} __attribute__((packed));

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VRTD_WIRE_H
