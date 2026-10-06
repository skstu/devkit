#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace libpath::utf8 {
    inline std::filesystem::path From(std::string_view value) {
        if (value.empty())
            return {};
        const auto* begin = reinterpret_cast<const char8_t*>(value.data());
        return std::filesystem::path(std::u8string(begin, begin + value.size()));
    }
    inline std::string To(const std::filesystem::path& value) {
        const auto bytes = value.u8string();
        return {bytes.begin(), bytes.end()};
    }
    inline std::string Generic(const std::filesystem::path& value) {
        const auto bytes = value.generic_u8string();
        return {bytes.begin(), bytes.end()};
    }
}
