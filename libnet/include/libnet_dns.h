#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace libnet::dns {
    using Bytes = std::vector<std::uint8_t>;
    inline bool AppendName(Bytes& output, std::string_view name) {
        std::size_t offset = 0;
        while (offset < name.size()) {
            const std::size_t end = name.find('.', offset);
            const std::size_t length =
                (end == std::string_view::npos ? name.size() : end) - offset;
            if (length == 0 || length > 63)
                return false;
            output.push_back(static_cast<std::uint8_t>(length));
            output.insert(output.end(),
                          name.begin() + static_cast<std::ptrdiff_t>(offset),
                          name.begin() + static_cast<std::ptrdiff_t>(offset + length));
            if (end == std::string_view::npos)
                break;
            offset = end + 1;
        }
        output.push_back(0);
        return true;
    }
    inline bool ReadName(std::span<const std::uint8_t> packet, std::size_t& offset,
                         std::string& name, std::uint32_t maximum_labels) {
        name.clear();
        std::size_t cursor = offset;
        std::size_t resume = 0;
        bool jumped = false;
        for (std::uint32_t labels = 0; labels < maximum_labels;
             ++labels) {
            if (cursor >= packet.size())
                return false;
            const std::uint8_t length = packet[cursor++];
            if (length == 0) {
                offset = jumped ? resume : cursor;
                return true;
            }
            if ((length & 0xc0) == 0xc0) {
                if (cursor >= packet.size())
                    return false;
                const std::size_t target =
                    static_cast<std::size_t>((length & 0x3f) << 8) | packet[cursor++];
                if (target >= packet.size())
                    return false;
                if (!jumped) {
                    resume = cursor;
                    jumped = true;
                }
                cursor = target;
                continue;
            }
            if ((length & 0xc0) != 0 || length > 63 || cursor > packet.size() ||
                packet.size() - cursor < length)
                return false;
            if (!name.empty())
                name.push_back('.');
            name.append(reinterpret_cast<const char*>(packet.data() + cursor), length);
            cursor += length;
        }
        return false;
    }
    inline void AppendTxt(Bytes& rdata, std::string_view value) {
        if (value.size() > 255)
            throw std::length_error("DNS TXT item");
        rdata.push_back(static_cast<std::uint8_t>(value.size()));
        rdata.insert(rdata.end(), value.begin(), value.end());
    }
}
