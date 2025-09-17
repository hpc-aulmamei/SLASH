#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include "vrtd/wire.h"
#endif

#include <vrtd/session.hpp>
#include <vrtd/error.hpp>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

namespace vrtd {

Session::Session(const char *socketPath)
: m(std::make_unique<std::mutex>()) {
    fd = vrtd_connect(socketPath);

    if (fd == -1) {
        throw std::runtime_error(std::string("Failed to open socket ") + strerrordesc_np(errno));
    }
}

Session::~Session() noexcept
{
    close();
}

Session::Session(Session&& other) noexcept
{
    if (!other.isClosed()) {
        std::lock_guard<std::mutex> lk(*other.m);

        fd = std::exchange(other.fd, -1);
        m = std::exchange(other.m, nullptr);
    } else {
        fd = -1;
        m = nullptr;
    }
}

Session& Session::operator=(Session&& other) noexcept
{
    close();

    if (!other.isClosed()) {
        std::lock_guard<std::mutex> lk(*other.m);

        fd = std::exchange(other.fd, -1);
        m = std::exchange(other.m, nullptr);
    }

    return *this;
}

uint32_t Session::getNumDevices() const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    uint32_t numDevices;

    auto ret = vrtd_get_num_devices(fd, &numDevices);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return numDevices;
}

Device Session::getDevice(size_t i) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    char name[128];

    auto ret = vrtd_get_device_info(fd, i, name);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return Device(i, {name, strlen(name)}, [&](const Device& device, uint8_t num) { return getBar(device, num); } );
}

Bar Session::getBar(const Device& device, uint8_t num) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    slash_ioctl_bar_info barInfo;

    auto ret = vrtd_get_bar_info(fd, device.getNum(), num, &barInfo);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return Bar(device.getNum(), num, barInfo.usable, barInfo.in_use, barInfo.start_address, barInfo.length, [&](const Bar&bar) { return openBarFile(bar); } );
}

BarFile Session::openBarFile(const Bar& bar) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    slash_bar_file barFile;

    auto ret = vrtd_open_bar_file(fd, bar.getDeviceNum(), bar.getNum(), &barFile);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return BarFile(barFile);
}

void Session::close() noexcept {
    if (isClosed()) {
        return;
    }

    ::close(fd);
    fd = -1;
    m = nullptr;
}

bool Session::isClosed() const noexcept {
    if (fd == -1 || !m) {
        return true;
    } else {
        return false;
    }
}

}
