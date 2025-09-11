#ifndef VRTD_BAR_FILE_PTR_HPP
#define VRTD_BAR_FILE_PTR_HPP

#include <functional>
#include <type_traits>
#include <cstddef>
#include <utility>

namespace vrtd {

template<class T>
class BarFilePtr {
    static_assert(std::is_object_v<T>, "T must be an object type");
public:
    using element_type = T;
    using pointer      = volatile T*;
    using callback_t   = std::function<void()>;

    // construct with an address and optional destructor callback
    explicit BarFilePtr(pointer p = nullptr, callback_t cb = {}) noexcept
        : p_(p), cb_(std::move(cb)) {}

    // move-only (ensures callback runs at most once)
    BarFilePtr(BarFilePtr&& other) noexcept
        : p_(other.p_), cb_(std::move(other.cb_)) {
        other.p_ = nullptr;
        other.cb_ = nullptr;
    }
    BarFilePtr& operator=(BarFilePtr&& other) noexcept {
        if (this != &other) {
            run_callback();
            p_  = other.p_;
            cb_ = std::move(other.cb_);
            other.p_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    BarFilePtr(const BarFilePtr&)            = delete;
    BarFilePtr& operator=(const BarFilePtr&) = delete;

    ~BarFilePtr() { run_callback(); }

    // ---- implicit conversions (only these two) ----
    operator pointer() const noexcept { return p_; }                  // to volatile T*
    operator volatile void*() const noexcept { return p_; }           // to volatile void*

    // ---- pointer-like interface ----
    pointer get()        const noexcept { return p_; }
    volatile T& operator*() const noexcept { return *p_; }
    pointer     operator->() const noexcept { return p_; }

    // index (useful for arrays / pointer arithmetic)
    volatile T& operator[](std::size_t i) const noexcept { return p_[i]; }

    // truthiness
    explicit operator bool() const noexcept { return p_ != nullptr; }

    // comparisons
    friend bool operator==(const BarFilePtr& a, const BarFilePtr& b) noexcept { return a.p_ == b.p_; }
    friend bool operator!=(const BarFilePtr& a, const BarFilePtr& b) noexcept { return !(a == b); }
    friend bool operator==(const BarFilePtr& a, std::nullptr_t) noexcept { return a.p_ == nullptr; }
    friend bool operator==(std::nullptr_t, const BarFilePtr& a) noexcept { return a.p_ == nullptr; }
    friend bool operator!=(const BarFilePtr& a, std::nullptr_t) noexcept { return a.p_ != nullptr; }
    friend bool operator!=(std::nullptr_t, const BarFilePtr& a) noexcept { return a.p_ != nullptr; }

    // optional: compare directly with a raw volatile T*
    friend bool operator==(const BarFilePtr& a, pointer p) noexcept { return a.p_ == p; }
    friend bool operator==(pointer p, const BarFilePtr& a) noexcept { return a.p_ == p; }
    friend bool operator!=(const BarFilePtr& a, pointer p) noexcept { return a.p_ != p; }
    friend bool operator!=(pointer p, const BarFilePtr& a) noexcept { return a.p_ != p; }

private:
    void run_callback() noexcept {
        if (cb_) {
            auto cb = std::move(cb_);
            cb_ = nullptr;      // ensure single fire
            cb(p_);
        }
    }

    pointer     p_  = nullptr;
    callback_t  cb_ = nullptr;
};

} // namsepace vrtd

#endif // VRTD_BAR_FILE_PTR_HPP
