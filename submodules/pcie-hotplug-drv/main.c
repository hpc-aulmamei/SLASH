#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#define PCIE_IOCTL_MAGIC       0xB5
#define PCIE_IOCTL_RESCAN      _IO(PCIE_IOCTL_MAGIC, 0x01)
#define PCIE_IOCTL_REMOVE      _IO(PCIE_IOCTL_MAGIC, 0x02)
#define PCIE_IOCTL_TOGGLE_SBR  _IO(PCIE_IOCTL_MAGIC, 0x03)
#define PCIE_IOCTL_HOTPLUG     _IO(PCIE_IOCTL_MAGIC, 0x04)

struct pcie_bar_read {
    uint32_t bar_index;
    uint32_t offset;
    uint32_t value;
};

struct pcie_bar_write {
    uint8_t bar_index;
    uint32_t offset;
    uint32_t value;
};

#define PCIE_IOCTL_GET_BAR_VAL _IOWR(PCIE_IOCTL_MAGIC, 0x05, struct pcie_bar_read)
#define PCIE_IOCTL_SET_BAR_VAL _IOWR(PCIE_IOCTL_MAGIC, 0x06, struct pcie_bar_write)

#define MAX_BAR_RW_SIZE 128

struct pcie_bar_range {
    uint8_t bar_index;
    uint32_t offset;
    uint32_t size;
    uint8_t data[MAX_BAR_RW_SIZE];
};

#define PCIE_IOCTL_READ_BAR_RANGE  _IOWR(PCIE_IOCTL_MAGIC, 0x07, struct pcie_bar_range)
#define PCIE_IOCTL_WRITE_BAR_RANGE _IOWR(PCIE_IOCTL_MAGIC, 0x08, struct pcie_bar_range)


void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s <devnode> <rescan|remove|toggle_sbr|hotplug>\n"
        "  %s <devnode> bar_read <bar_index> <offset>\n"
        "  %s <devnode> bar_write <bar_index> <offset> <value>\n"
        "Example:\n"
        "  %s /dev/pcie_hotplug_0000:c4:00.0 remove\n"
        "  %s /dev/pcie_hotplug_0000:c4:00.0 bar_read 0 0x10\n"
        "  %s /dev/pcie_hotplug_0000:c4:00.0 bar_write 0 0x10 0xdeadbeef\n",
        prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    int fd, ret;
    unsigned long cmd = 0;

    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *devnode = argv[1];
    const char *action = argv[2];

    fd = open(devnode, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    if (strcmp(action, "rescan") == 0)
        cmd = PCIE_IOCTL_RESCAN;
    else if (strcmp(action, "remove") == 0)
        cmd = PCIE_IOCTL_REMOVE;
    else if (strcmp(action, "toggle_sbr") == 0)
        cmd = PCIE_IOCTL_TOGGLE_SBR;
    else if (strcmp(action, "hotplug") == 0)
        cmd = PCIE_IOCTL_HOTPLUG;
    else if (strcmp(action, "bar_read") == 0) {
        if (argc != 5) {
            fprintf(stderr, "Usage: %s <devnode> bar_read <bar_index> <offset>\n", argv[0]);
            close(fd);
            return EXIT_FAILURE;
        }

        struct pcie_bar_read bar = {0};
        bar.bar_index = strtoul(argv[3], NULL, 0);
        bar.offset = strtoul(argv[4], NULL, 0);

        ret = ioctl(fd, PCIE_IOCTL_GET_BAR_VAL, &bar);
        if (ret < 0) {
            if (errno == EINVAL)
                fprintf(stderr, "Invalid BAR index or offset\n");
            else
                perror("ioctl failed");
            close(fd);
            return EXIT_FAILURE;
        }

        printf("BAR[%u] offset 0x%x: value = 0x%08x\n",
               bar.bar_index, bar.offset, bar.value);
        close(fd);
        return EXIT_SUCCESS;
    } else if (strcmp(action, "bar_write") == 0) {
        if (argc != 6) {
            fprintf(stderr, "Usage: %s <devnode> bar_write <bar_index> <offset> <value>\n", argv[0]);
            close(fd);
            return EXIT_FAILURE;
        }

        struct pcie_bar_write bar = {0};
        bar.bar_index = strtoul(argv[3], NULL, 0);
        bar.offset = strtoul(argv[4], NULL, 0);
        bar.value = strtoul(argv[5], NULL, 0);

        ret = ioctl(fd, PCIE_IOCTL_SET_BAR_VAL, &bar);
        if (ret < 0) {
            if (errno == EINVAL)
                fprintf(stderr, "Invalid BAR index or offset\n");
            else
                perror("ioctl failed");
            close(fd);
            return EXIT_FAILURE;
        }

        printf("Wrote 0x%08x to BAR[%u] offset 0x%x\n",
               bar.value, bar.bar_index, bar.offset);
        close(fd);
        return EXIT_SUCCESS;
    } else if (strcmp(action, "bar_read_range") == 0) {
        if (argc != 6) {
            fprintf(stderr, "Usage: %s <devnode> bar_read_range <bar_index> <offset> <size>\n", argv[0]);
            close(fd);
            return EXIT_FAILURE;
        }

        struct pcie_bar_range range = {0};
        range.bar_index = strtoul(argv[3], NULL, 0);
        range.offset = strtoul(argv[4], NULL, 0);
        range.size = strtoul(argv[5], NULL, 0);
        if (range.size > MAX_BAR_RW_SIZE) {
            fprintf(stderr, "Max range read size is %d bytes\n", MAX_BAR_RW_SIZE);
            close(fd);
            return EXIT_FAILURE;
        }

        ret = ioctl(fd, PCIE_IOCTL_READ_BAR_RANGE, &range);
        if (ret < 0) {
            perror("ioctl failed");
            close(fd);
            return EXIT_FAILURE;
        }

        printf("BAR[%u] offset 0x%x, size %u:\n", range.bar_index, range.offset, range.size);
        for (uint32_t i = 0; i < range.size; i++) {
            printf("%02x ", range.data[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (range.size % 16 != 0) printf("\n");

        close(fd);
        return EXIT_SUCCESS;
    } else if (strcmp(action, "bar_write_range") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Usage: %s <devnode> bar_write_range <bar_index> <offset> <size> <byte0> [byte1 ...]\n", argv[0]);
            close(fd);
            return EXIT_FAILURE;
        }

        struct pcie_bar_range range = {0};
        range.bar_index = strtoul(argv[3], NULL, 0);
        range.offset = strtoul(argv[4], NULL, 0);
        range.size = strtoul(argv[5], NULL, 0);

        if (range.size > MAX_BAR_RW_SIZE || argc != 6 + range.size) {
            fprintf(stderr, "Provide exactly %u bytes\n", range.size);
            close(fd);
            return EXIT_FAILURE;
        }

        for (uint32_t i = 0; i < range.size; i++) {
            range.data[i] = (uint8_t)strtoul(argv[6 + i], NULL, 0);
        }

        ret = ioctl(fd, PCIE_IOCTL_WRITE_BAR_RANGE, &range);
        if (ret < 0) {
            perror("ioctl failed");
            close(fd);
            return EXIT_FAILURE;
        }

        printf("Wrote %u bytes to BAR[%u] offset 0x%x\n", range.size, range.bar_index, range.offset);
        close(fd);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Unknown command: %s\n", action);
        usage(argv[0]);
        close(fd);
        return EXIT_FAILURE;
    }

    ret = ioctl(fd, cmd);
    if (ret < 0) {
        perror("ioctl failed");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Command '%s' executed successfully on %s\n", action, devnode);
    close(fd);
    return EXIT_SUCCESS;
}