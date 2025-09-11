#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <vrtd/session.hpp>
#include <vrtd/error.hpp>

#include <string.h>
#include <errno.h>

namespace vrtd {

Session::Session(const char *socketPath) {
    fd = vrtd_connect(socketPath);

    if (fd == -1) {
        throw std::runtime_error(std::string("Failed to open socket ") + strerrordesc_np(errno));
    }
}

size_t Session::getNumDevices() {
    uint32_t numDevices;

    auto ret = vrtd_get_num_devices(fd, &numDevices);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return numDevices;
}

Device Session::getDevice(size_t i) {
    char name[128];

    auto ret = vrtd_get_device_info(fd, i, name);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return Device(i, {name, strlen(name)}, [&](const Device& device, uint8_t num) { return getBar(device, num); } );
}

Bar Session::getBar(const Device& device, uint8_t num) {
    slash_ioctl_bar_info barInfo;

    auto ret = vrtd_get_bar_info(fd, device.getNum(), num, &barInfo);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return Bar(device.getNum(), num, barInfo.usable, barInfo.in_use, barInfo.start_address, barInfo.length, [&](const Bar&bar) { return openBarFile(bar); } );
}

BarFile Session::openBarFile(const Bar& bar) {
    slash_bar_file barFile;

    auto ret = vrtd_open_bar_file(fd, bar.getDeviceNum(), bar.getNum(), &barFile);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return BarFile(barFile);
}

}
