#define _GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>

#include <slash/ctldev.h>

int main()
{
    struct slash_ioctl_bar_info *bar_info;
    struct slash_bar_file *bar_file;
    struct slash_ctldev *ctldev;

    ctldev = slash_ctldev_open("/dev/slash_ctl0");
    if (ctldev == NULL) {
        perror("1");
        return 1;
    }

    for (int i = 0; i < 6; i++) {
        bar_info = slash_bar_info_read(ctldev, i);
        if (bar_info == NULL) {
            perror("2");
            continue;
        } else {
            printf("BAR Info:\n");
            printf("  bar_number: %d\n", bar_info->bar_number);
            printf("  usable: %s\n", bar_info->usable ? "true" : "false");
            printf("  in_use: %s\n", bar_info->in_use ? "true" : "false");
            printf("  start_address: 0x%llx\n", bar_info->start_address);
            printf("  length: 0x%llx\n", bar_info->length);

            if (bar_info->usable) {
                volatile uint32_t *p;
                uint32_t val;

                bar_file = slash_bar_file_open(ctldev, i, O_CLOEXEC);
                if (bar_file == NULL) {
                    perror("3");
                    continue;
                }

                p = bar_file->map;

                slash_bar_file_start_write(bar_file);

                p[0] = 1;

                slash_bar_file_end_write(bar_file);

                slash_bar_file_start_read(bar_file);

                val = p[0];

                slash_bar_file_end_read(bar_file);

                printf("BAR%d: wrote 1 read back %u\n", i, val);

                if (slash_bar_file_close(bar_file) != 0) {
                    perror("4");
                }
            }

            slash_bar_info_free(bar_info);
        }
    }

    if (slash_ctldev_close(ctldev) != 0) {
        perror("5");
        return 1;
    }
    return 0;
}
