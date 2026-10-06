#pragma once
#include <chrono>
#include <cstdint>
namespace stl {
    inline std::uint64_t UnixSeconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }
    inline std::uint64_t UnixMilliseconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }
    inline std::uint64_t SteadySeconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    inline std::uint64_t SteadyMilliseconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
} // namespace stl
