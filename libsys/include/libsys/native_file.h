#pragma once
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace libsys {
    // Owns one native file handle. Lock lifetime follows this object's lifetime.
    class NativeFile final {
    public:
#ifdef _WIN32
        using Type = HANDLE;
        static Type Invalid() noexcept {
            return INVALID_HANDLE_VALUE;
        }
#else
        using Type = int;
        static constexpr Type Invalid() noexcept {
            return -1;
        }
#endif
        Type value = Invalid();
        NativeFile() = default;
        explicit NativeFile(Type file) noexcept : value(file) {
        }
        NativeFile(const NativeFile&) = delete;
        NativeFile& operator=(const NativeFile&) = delete;
        NativeFile(NativeFile&& other) noexcept : value(std::exchange(other.value, Invalid())) {
        }
        NativeFile& operator=(NativeFile&& other) noexcept {
            if (this != &other)
                reset(std::exchange(other.value, Invalid()));
            return *this;
        }
        ~NativeFile() {
            reset();
        }
        Type get() const noexcept {
            return value;
        }
        void reset(Type file = Invalid()) noexcept {
            if (file == value)
                return;
#ifdef _WIN32
            if (value != Invalid() && value != nullptr)
                CloseHandle(value);
#else
            if (value >= 0)
                ::close(value);
#endif
            value = file;
        }
    };
}
