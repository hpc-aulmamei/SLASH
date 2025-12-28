#include <slash/ctldev.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>

static int test_bar_info(struct slash_ctldev *ctldev)
{
    struct slash_ioctl_bar_info *bar0;
    struct slash_ioctl_bar_info *bar1;

    bar0 = slash_bar_info_read(ctldev, 0);
    if (bar0 == NULL) {
        fprintf(stderr, "slash_bar_info_read returned NULL for bar 0\n");
        return 1;
    }

    if (bar0->bar_number != 0) {
        fprintf(stderr, "Unexpected bar number %u for bar 0\n", (unsigned) bar0->bar_number);
        slash_bar_info_free(bar0);
        return 1;
    }

    if (!bar0->usable || bar0->length == 0) {
        fprintf(stderr, "Mock bar 0 should be usable with non-zero length\n");
        slash_bar_info_free(bar0);
        return 1;
    }

    slash_bar_info_free(bar0);

    bar1 = slash_bar_info_read(ctldev, 1);
    if (bar1 == NULL) {
        fprintf(stderr, "slash_bar_info_read returned NULL for bar 1\n");
        return 1;
    }

    if (bar1->usable || bar1->length != 0) {
        fprintf(stderr, "Mock bar 1 should be unusable with zero length\n");
        slash_bar_info_free(bar1);
        return 1;
    }

    slash_bar_info_free(bar1);

    return 0;
}

static int test_bar_file(struct slash_ctldev *ctldev)
{
    struct slash_bar_file *bar;
    uint32_t *mapped;

    bar = slash_bar_file_open(ctldev, 0, O_RDWR);
    if (bar == NULL) {
        fprintf(stderr, "slash_bar_file_open failed for bar 0\n");
        return 1;
    }

    if (!bar->mock) {
        fprintf(stderr, "Mock bar file should be marked as mock\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (bar->len == 0 || bar->map == NULL) {
        fprintf(stderr, "Mock bar file should expose a mapped region\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    mapped = (uint32_t *) bar->map;
    mapped[0] = 0xA5A5A5A5U;
    if (mapped[0] != 0xA5A5A5A5U) {
        fprintf(stderr, "Mock BAR memory did not retain written value\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (slash_bar_file_start_write(bar) != 0) {
        fprintf(stderr, "slash_bar_file_start_write failed for mock BAR\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (slash_bar_file_end_write(bar) != 0) {
        fprintf(stderr, "slash_bar_file_end_write failed for mock BAR\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (slash_bar_file_start_read(bar) != 0) {
        fprintf(stderr, "slash_bar_file_start_read failed for mock BAR\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (slash_bar_file_end_read(bar) != 0) {
        fprintf(stderr, "slash_bar_file_end_read failed for mock BAR\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (slash_bar_file_close(bar) != 0) {
        fprintf(stderr, "slash_bar_file_close failed for mock BAR\n");
        return 1;
    }

    errno = 0;
    bar = slash_bar_file_open(ctldev, 1, O_RDONLY);
    if (bar != NULL) {
        fprintf(stderr, "Mock bar file open should fail for bar 1\n");
        (void) slash_bar_file_close(bar);
        return 1;
    }

    if (errno != ENODEV) {
        fprintf(stderr, "Expected ENODEV when opening mock bar 1, got %d\n", errno);
        return 1;
    }

    return 0;
}

int main(void)
{
    struct slash_ctldev *ctldev;

    ctldev = slash_ctldev_open("@mock");
    if (ctldev == NULL) {
        fprintf(stderr, "slash_ctldev_open failed for @mock\n");
        return 1;
    }

    if (!ctldev->mock) {
        fprintf(stderr, "Mock device should set mock flag\n");
        (void) slash_ctldev_close(ctldev);
        return 1;
    }

    if (test_bar_info(ctldev) != 0) {
        (void) slash_ctldev_close(ctldev);
        return 1;
    }

    if (test_bar_file(ctldev) != 0) {
        (void) slash_ctldev_close(ctldev);
        return 1;
    }

    if (slash_ctldev_close(ctldev) != 0) {
        fprintf(stderr, "slash_ctldev_close failed for mock device\n");
        return 1;
    }

    printf("libslash mock tests passed\n");
    return 0;
}
