#include <vrtd/bar_file.hpp>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace vrtd {

BarFile::BarFile(slash_bar_file barFile) noexcept {
    this->barFile = barFile;
}

BarFile::~BarFile() {
    close();
}

void BarFile::close() {
    if (closed) {
        return;
    }

    if (reading || writing) {
        throw std::runtime_error("Bar file closed while in memory operation");
    }

    munmap(barFile.map, barFile.len);
    ::close(barFile.fd);
}

size_t BarFile::getLen() const noexcept {
    if (closed) {
        return 0;
    }

    return barFile.len;
}

volatile void *BarFile::getRawPtr(size_t address) const noexcept {
    if (closed) {
        return nullptr;
    }

    if (address >= barFile.len) {
        return nullptr;
    }

    volatile uint8_t *p = static_cast<volatile uint8_t *>(barFile.map);

    return &p[address];
}

}
