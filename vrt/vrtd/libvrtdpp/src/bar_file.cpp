#include <vrtd/bar_file.hpp>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <utility>

namespace vrtd {

BarFile::BarFile(slash_bar_file barFile) noexcept {
    this->barFile = barFile;
}

BarFile::~BarFile() {
    close();
}

BarFile::BarFile(BarFile&& other) noexcept {
    barFile = std::exchange(other.barFile, {});
    reading = std::exchange(other.reading, false);
    writing = std::exchange(other.writing, false);
    closed  = std::exchange(other.closed, true);
}

BarFile& BarFile::operator=(BarFile&& other) noexcept {
    close();

    barFile = std::exchange(other.barFile, {});
    reading = std::exchange(other.reading, false);
    writing = std::exchange(other.writing, false);
    closed  = std::exchange(other.closed, true);

    return *this;
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

bool BarFile::isClosed() const noexcept {
    return closed;
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
