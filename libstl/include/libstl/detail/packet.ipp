#pragma once
#include <cstring>
#include <limits>

namespace stl {
	namespace detail {
// Legacy native-endian/native-unsigned-long format; retain its existing layout.
#pragma pack(push, 1)
		struct PacketHeader {
			unsigned long head, count, payload_size, tail;
		};
		struct PacketEntry {
			unsigned long key, position, size;
		};
#pragma pack(pop)
	} // namespace detail

	LIBSTL_INLINE bool Packet::Made(const Map& input, std::string& output) const {
		output.clear();
		constexpr auto limit = std::numeric_limits<unsigned long>::max();
		if (input.empty() || input.size() > limit)
			return false;
		std::size_t total = sizeof(detail::PacketHeader);
		unsigned long payload_size = 0;
		for (const auto& [key, value] : input) {
			(void)key;
			if (total > limit - sizeof(detail::PacketEntry) ||
			    value.size() > limit - total - sizeof(detail::PacketEntry))
				return false;
			total += sizeof(detail::PacketEntry) + value.size();
			payload_size += static_cast<unsigned long>(value.size());
		}
		std::string result;
		result.reserve(total);
		const detail::PacketHeader header{0xFEFF, static_cast<unsigned long>(input.size()),
		                                  payload_size, 0x200B};
		result.append(reinterpret_cast<const char*>(&header), sizeof(header));
		for (const auto& [key, value] : input) {
			const detail::PacketEntry entry{key, static_cast<unsigned long>(result.size()),
			                                static_cast<unsigned long>(value.size())};
			result.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
			result.append(value);
		}
		output.swap(result);
		return true;
	}

	LIBSTL_INLINE bool Packet::UnMade(const std::string& input, Map& output) const {
		output.clear();
		if (input.size() < sizeof(detail::PacketHeader))
			return false;
		detail::PacketHeader header{};
		std::memcpy(&header, input.data(), sizeof(header));
		std::size_t offset = sizeof(header);
		if (header.head != 0xFEFF || header.tail != 0x200B || header.count == 0 ||
		    header.count > (input.size() - offset) / sizeof(detail::PacketEntry))
			return false;
		const auto metadata_size = static_cast<std::size_t>(header.count) * sizeof(detail::PacketEntry);
		if (header.payload_size != input.size() - offset - metadata_size)
			return false;
		Map result;
		for (unsigned long i = 0; i < header.count; ++i) {
			if (input.size() - offset < sizeof(detail::PacketEntry))
				return false;
			detail::PacketEntry entry{};
			std::memcpy(&entry, input.data() + offset, sizeof(entry));
			if (entry.position != offset || entry.key > std::numeric_limits<Key>::max())
				return false;
			offset += sizeof(entry);
			if (entry.size > input.size() - offset)
				return false;
			if (!result.emplace(static_cast<Key>(entry.key), input.substr(offset, entry.size)).second)
				return false;
			offset += entry.size;
		}
		if (offset != input.size())
			return false;
		output.swap(result);
		return true;
	}
} // namespace stl
