#pragma once
#include <cstdint>
#include <string_view>

namespace stl {
    inline bool IsValidUtf8(std::string_view value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
        std::size_t index = 0;
        while (index < value.size()) {
            const unsigned char first = bytes[index++];
            if (first <= 0x7f)
                continue;
            int trailing = 0;
            std::uint32_t codepoint = 0;
            if ((first & 0xe0) == 0xc0) {
                trailing = 1;
                codepoint = first & 0x1f;
                if (codepoint < 2)
                    return false;
            }
            else if ((first & 0xf0) == 0xe0) {
                trailing = 2;
                codepoint = first & 0x0f;
            }
            else if ((first & 0xf8) == 0xf0) {
                trailing = 3;
                codepoint = first & 0x07;
            }
            else {
                return false;
            }
            if (index + trailing > value.size())
                return false;
            for (int count = 0; count < trailing; ++count) {
                const unsigned char next = bytes[index++];
                if ((next & 0xc0) != 0x80)
                    return false;
                codepoint = (codepoint << 6) | (next & 0x3f);
            }
            if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
                (trailing == 2 && codepoint < 0x800) ||
                (trailing == 3 && codepoint < 0x10000))
                return false;
        }
        return true;
    }
} // namespace stl
