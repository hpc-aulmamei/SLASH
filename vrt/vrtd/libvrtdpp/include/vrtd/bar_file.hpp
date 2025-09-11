#ifndef VRTD_BAR_FILE_HPP
#define VRTD_BAR_FILE_HPP

#include <slash_driver/ctldev.h>

#include <vrtd/bar_file_ptr.hpp>

#include <stdexcept>

namespace vrtd {

class BarFile {
public:
    ~BarFile();

    size_t getLen() const noexcept;
    volatile void *getRawPtr(size_t address = 0) const noexcept;

    void close();

private:
    friend class Session;
    explicit BarFile(slash_bar_file barFile) noexcept;

    slash_bar_file barFile;

    bool reading{};
    bool writing{};
    bool closed{};

public:
    enum class Direction {
        Read,
        Write,
    };

    template<class T>
    BarFilePtr<T> getPtr(Direction direction, size_t address = 0) {
        if (closed) {
            throw std::runtime_error("Memory operation on closed bar file");
        }

        if (address >= barFile.len) {
            throw std::runtime_error("Bad address");
        }

        if (reading || writing) {
            throw std::runtime_error("Memory operation already in progress");
        }

        volatile uint8_t *p = static_cast<volatile uint8_t *>(barFile.map);
        volatile T *paddr = static_cast<volatile T *>(&p[address]);

        std::function<void()> callback{};

        if (direction == Direction::Read) {
            slash_bar_file_start_read(&barFile);
            reading = true;
            callback = [&]{
                slash_bar_file_end_read(&barFile);
                reading = false;
            };
        } else if (direction == Direction::Write) {
            slash_bar_file_start_write(&barFile);
            writing = true;
            callback = [&]{
                slash_bar_file_end_write(&barFile);
                writing = false;
            };
        } else {
            throw std::runtime_error("Bad direction");
        }

        return BarFilePtr(paddr, callback);
    }
};

} // namespace vrtd

#endif // VRTD_BAR_FILE_HPP
