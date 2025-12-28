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

slash_qdma_info Session::getQdmaInfo(const Device& device) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    slash_qdma_info info;
    auto ret = vrtd_qdma_get_info(fd, device.getNum(), &info);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return info;
}

QdmaQpair Session::createQdmaQpair(
    const Device& device,
    const slash_qdma_qpair_add& cfg
) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    slash_qdma_qpair_add tmp = cfg;
    auto ret = vrtd_qdma_qpair_add(fd, device.getNum(), &tmp);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return QdmaQpair(
        device.getNum(),
        tmp.qid,
        [this, device](const QdmaQpair& qp) { startQdmaQpair(device, qp.getQid()); },
        [this, device](const QdmaQpair& qp) { stopQdmaQpair(device, qp.getQid()); },
        [this, device](const QdmaQpair& qp) { deleteQdmaQpair(device, qp.getQid()); },
        [this, device](const QdmaQpair& qp, uint32_t flags) { return openQdmaQpairFd(device, qp.getQid(), flags); }
    );
}

void Session::startQdmaQpair(const Device& device, uint32_t qid) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    auto ret = vrtd_qdma_qpair_start(fd, device.getNum(), qid);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }
}

void Session::stopQdmaQpair(const Device& device, uint32_t qid) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    auto ret = vrtd_qdma_qpair_stop(fd, device.getNum(), qid);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }
}

void Session::deleteQdmaQpair(const Device& device, uint32_t qid) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    auto ret = vrtd_qdma_qpair_del(fd, device.getNum(), qid);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }
}

int Session::openQdmaQpairFd(const Device& device, uint32_t qid, uint32_t flags) const {
    if (isClosed()) {
        throw Error(VRTD_RET_BAD_LIB_CALL);
    }
    std::lock_guard<std::mutex> lk(*m);

    int qfd = -1;
    auto ret = vrtd_qdma_qpair_get_fd(fd, device.getNum(), qid, flags, &qfd);
    if (ret != VRTD_RET_OK) {
        throw Error(ret);
    }

    return qfd;
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
