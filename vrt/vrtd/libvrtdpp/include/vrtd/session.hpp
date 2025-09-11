#ifndef VRTD_SESSION_HPP
#define VRTD_SESSION_HPP

#include <stdint.h>
#include <vrtd/vrtd.h>
#include <vrtd/device.hpp>
#include <vrtd/bar.hpp>
#include <vrtd/bar_file.hpp>

namespace vrtd {

class Session {
public:
    explicit Session(const char *socket_path = VRTD_STANADRD_PATH);

    size_t getNumDevices();

    Device getDevice(size_t i);
private:
    int fd;

    // This will be called through the helper in the Device class
    Bar getBar(const Device& device, uint8_t bar_number);

    // This will be called through the helper in the Bar class
    BarFile openBarFile(const Bar &bar);
};

}

#endif // VRTD_VRTD_HPP
