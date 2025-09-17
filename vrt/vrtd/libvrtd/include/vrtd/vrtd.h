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
 * @file vrtd.h
 * @brief C client API for the V80 Runtime Daemon (vrtd).
 *
 * This library (libvrtd) provides a client interface to the VRT daemon (vrtd),
 * which multiplexes access to SLASH-managed FPGA devices
 * with permission control and multi‑tenancy.
 *
 * Stack overview:
 *   slash (kernel module) <- libslash <- vrtd <- libvrtd <- libvrtdpp <- libvrt
 *
 * Most functions return a #vrtd_ret code. On success, functions return
 * #VRTD_RET_OK and populate their output parameters.
 */

#ifndef LIBVRTD_VRTD_H
#define LIBVRTD_VRTD_H

#include <slash_driver/ctldev.h>
#include <vrtd/wire.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @def VRTD_STANADRD_PATH
 * @brief Default UNIX domain socket path for the vrtd daemon.
 */
#define VRTD_STANDARD_PATH "/run/vrtd.sock"


/**
 * @brief Connect to the vrtd UNIX domain socket.
 *
 * Creates a SOCK_SEQPACKET connection to the vrtd daemon at @p path.
 *
 * @param path Absolute path to the vrtd socket (e.g. ::VRTD_STANADRD_PATH).
 *             Must not be NULL.
 * @return On success, a non‑negative file descriptor to the socket. The caller
 *         owns this descriptor and must close it with @c close().
 * @return On failure, returns -1 and sets @c errno.
 */
int vrtd_connect(const char *path);


/**
 * @brief Send a raw vrtd protocol request and receive the response.
 *
 * This is a low‑level escape hatch for issuing arbitrary protocol opcodes.
 * Most users should prefer higher‑level helpers (e.g., vrtd_get_* functions).
 *
 * @param fd            Connected vrtd socket file descriptor.
 * @param opcode        Protocol opcode to send (see @ref vrtd_req_opcode in wire.h).
 * @param body          Pointer to request body buffer (may be NULL if @p body_size == 0).
 * @param body_size     Size of request body in bytes.
 * @param resp_buf      Buffer to receive the response body (may be NULL if no body expected).
 * @param resp_bufsz    Size of @p resp_buf in bytes.
 * @param out_fd        Optional; if non‑NULL and the response carries a file
 *                      descriptor (e.g., GET_BAR_FD), the received FD will be
 *                      stored here. Otherwise ignored.
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 *
 * @warning The request size must not exceed the protocol limit
 *          (e.g., @c VRTD_MSG_MAX_SIZE - sizeof(struct vrtd_req_header)).
 * @note    On success, @p resp_buf contains exactly the response body bytes.
 * @note    Only @p out_fd is optional among output parameters.
 */
enum vrtd_ret vrtd_raw_request(
    int fd,
    uint16_t opcode,
    const void *body, uint16_t body_size,
    void *resp_buf, size_t resp_bufsz,
    int *out_fd
);


/**
 * @brief Query the number of available devices.
 *
 * @param fd                 Connected vrtd socket file descriptor.
 * @param num_devices_out    Output pointer to receive the device count.
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 * @pre @p num_devices_out must not be NULL.
 */
enum vrtd_ret vrtd_get_num_devices(
    int fd,
    uint32_t *num_devices_out
);

/**
 * @brief Get information about a device (e.g., its name).
 *
 * @param fd          Connected vrtd socket file descriptor.
 * @param dev         Device index (0‑based).
 * @param name_out    Output buffer for the device name. Must be at least 128 bytes.
 *                    The string is NUL‑terminated and may be shorter than 128.
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 * @pre @p name_out must not be NULL and must have space for 128 bytes.
 */
enum vrtd_ret vrtd_get_device_info(
    int fd,
    uint32_t dev,
    char name_out[128]
);

/**
 * @brief Retrieve information about a device BAR (Base Address Register).
 *
 * Complementary to vrtd_get_bar_fd(); this returns metadata only.
 *
 * @param fd             Connected vrtd socket file descriptor.
 * @param dev            Device index (0‑based).
 * @param bar            BAR index.
 * @param bar_info_out   Output pointer for BAR info (layout, permissions, etc.).
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 * @pre @p bar_info_out must not be NULL.
 */
enum vrtd_ret vrtd_get_bar_info(
    int fd,
    uint32_t dev,
    uint8_t bar,
    struct slash_ioctl_bar_info *bar_info_out
);

/**
 * @brief Obtain a file descriptor for a device BAR, suitable for @c mmap().
 *
 * Complementary to vrtd_get_bar_info(); this returns a handle to the BAR memory.
 *
 * @param fd       Connected vrtd socket file descriptor.
 * @param dev      Device index (0‑based).
 * @param bar      BAR index.
 * @param fd_out   Output pointer to receive the BAR file descriptor.
 * @param len_out  Output pointer to receive the BAR length in bytes.
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 * @pre @p fd_out and @p len_out must not be NULL.
 * @note The caller owns the returned FD and should close it when no longer needed
 *       (or use vrtd_open_bar_file()/vrtd_close_bar_file()).
 */
enum vrtd_ret vrtd_get_bar_fd(
    int fd,
    uint32_t dev,
    uint8_t bar,
    int *fd_out,
    uint64_t *len_out
);

/**
 * @brief Open a BAR and map it into the process address space.
 *
 * Convenience helper that requests a BAR FD and performs @c mmap() into
 * @p bar_file_out->map with length @p bar_file_out->len.
 *
 * @param fd             Connected vrtd socket file descriptor.
 * @param dev            Device index (0‑based).
 * @param bar            BAR index.
 * @param bar_file_out   Output structure receiving the BAR FD, length and mapping.
 *
 * @return #VRTD_RET_OK on success; otherwise a #vrtd_ret error code.
 * @pre @p bar_file_out must not be NULL.
 * @post On success, @p bar_file_out->fd is valid and @p bar_file_out->map is
 *       a writable shared mapping of size @p bar_file_out->len.
 * @warning The caller must later call vrtd_close_bar_file() to unmap and close.
 */
enum vrtd_ret vrtd_open_bar_file(
    int fd,
    uint32_t dev,
    uint8_t bar,
    struct slash_bar_file *bar_file_out
);

/**
 * @brief Unmap and close resources acquired by vrtd_open_bar_file().
 *
 * Safe to call with NULL and safe to call multiple times; on first successful
 * call it unmaps, closes the FD, and clears @p bar_file->map.
 *
 * @param bar_file  Pointer previously filled by vrtd_open_bar_file().
 */
void vrtd_close_bar_file(
    struct slash_bar_file *bar_file_out
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBVRTD_VRTD_H
