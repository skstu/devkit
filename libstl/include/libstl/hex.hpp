#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stl {
    enum class HexCase
    {
        LowerOnly,
        Either
    };
    inline int HexNibble(char c, HexCase policy = HexCase::LowerOnly) noexcept {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (policy == HexCase::Either && c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
    inline bool IsHex(std::string_view text, HexCase policy = HexCase::LowerOnly) noexcept {
        for (char c : text)
            if (HexNibble(c, policy) < 0)
                return false;
        return true;
    }
    inline std::string Hex(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > std::string{}.max_size() / 2)
            throw std::length_error("hex size");
        constexpr char digits[] = "0123456789abcdef";
        std::string out(bytes.size() * 2, '0');
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out[2 * i] = digits[bytes[i] >> 4];
            out[2 * i + 1] = digits[bytes[i] & 15];
        }
        return out;
    }
    // Failure leaves output unchanged. Empty input/output is a successful decode.
    inline bool Unhex(std::string_view text, std::span<std::uint8_t> out,
                      HexCase policy = HexCase::LowerOnly) noexcept {
        if (text.size() % 2 || text.size() / 2 != out.size() || !IsHex(text, policy))
            return false;
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<std::uint8_t>((HexNibble(text[2 * i], policy) << 4) |
                                               HexNibble(text[2 * i + 1], policy));
        return true;
    }
} // namespace stl
