#pragma once
#include <array>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <vector>
namespace stl::binary {
    template <std::unsigned_integral T>
        requires(!std::same_as<T, bool>)
    inline void Append(std::vector<std::uint8_t>& out, T value) {
        for (std::size_t i = sizeof(T); i; --i)
            out.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
    }
    template <std::unsigned_integral T>
    inline bool Read(std::span<const std::uint8_t> input, std::size_t& offset, T& value) noexcept {
        if (offset > input.size() || input.size() - offset < sizeof(T))
            return false;
        T result = 0;
        for (auto byte : input.subspan(offset, sizeof(T)))
            result = static_cast<T>((result << 8) | byte);
        value = result;
        offset += sizeof(T);
        return true;
    }
    template <std::unsigned_integral T>
    inline T Read(std::span<const std::uint8_t> input, std::size_t offset = 0) {
        T result{};
        if (!Read(input, offset, result))
            throw std::out_of_range("binary read");
        return result;
    }
    inline void AppendU16(std::vector<std::uint8_t>& o, std::uint16_t v) {
        Append(o, v);
    }
    inline void AppendU32(std::vector<std::uint8_t>& o, std::uint32_t v) {
        Append(o, v);
    }
    inline void AppendU64(std::vector<std::uint8_t>& o, std::uint64_t v) {
        Append(o, v);
    }
    class Reader final {
    public:
        explicit Reader(std::span<const std::uint8_t> input) : bytes_(input) {
        }
        bool Read(void* out, std::size_t size) noexcept {
            if (size > remaining() || (size && !out))
                return false;
            if (size)
                std::memcpy(out, bytes_.data() + offset_, size);
            offset_ += size;
            return true;
        }
        bool U16(std::uint16_t& value) noexcept {
            return binary::Read(bytes_, offset_, value);
        }
        bool U32(std::uint32_t& value) noexcept {
            return binary::Read(bytes_, offset_, value);
        }
        bool U64(std::uint64_t& value) noexcept {
            return binary::Read(bytes_, offset_, value);
        }
        std::size_t remaining() const noexcept {
            return bytes_.size() - offset_;
        }

    private:
        std::span<const std::uint8_t> bytes_;
        std::size_t offset_ = 0;
    };
    inline bool ReadExact(std::istream& stream, void* output, std::size_t size) {
        if (size > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
            return false;
        stream.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
        return stream.gcount() == static_cast<std::streamsize>(size);
    }
    template <std::unsigned_integral T>
    inline bool Read(std::istream& stream, T& value) {
        std::array<std::uint8_t, sizeof(T)> bytes{};
        if (!ReadExact(stream, bytes.data(), bytes.size()))
            return false;
        value = Read<T>(bytes);
        return true;
    }
    template <std::unsigned_integral T>
    inline void Write(std::ostream& stream, T value) {
        std::array<char, sizeof(T)> bytes{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = static_cast<char>(value >> ((sizeof(T) - i - 1) * 8));
        stream.write(bytes.data(), bytes.size());
    }
} // namespace stl::binary
