#ifndef LIBVRTD_VRTD_H
#define LIBVRTD_VRTD_H

#include <slash_driver/ctldev.h>
#include <vrtd/wire.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VRTD_STANADRD_PATH "/run/vrtd.sock"

int vrtd_connect(const char *path);

enum vrtd_ret vrtd_raw_request(
    int fd,
    uint16_t opcode,
    const void *body, uint16_t body_size,
    void *resp_buf, size_t resp_bufsz,
    int *out_fd // may be NULL; only used for GET_BAR_FD
);

enum vrtd_ret vrtd_get_num_devices(
    int fd,
    uint32_t *num_devices_out
);

enum vrtd_ret vrtd_get_device_info(
    int fd,
    uint32_t dev,
    char name_out[128]
);

enum vrtd_ret vrtd_get_bar_info(
    int fd,
    uint32_t dev,
    uint8_t bar,
    struct slash_ioctl_bar_info *bar_info_out
);

enum vrtd_ret vrtd_get_bar_fd(
    int fd,
    uint32_t dev,
    uint8_t bar,
    int *fd_out,
    uint64_t *len_out
);

enum vrtd_ret vrtd_open_bar_file(
    int fd,
    uint32_t dev,
    uint8_t bar,
    struct slash_bar_file *bar_file_out
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBVRTD_VRTD_H
