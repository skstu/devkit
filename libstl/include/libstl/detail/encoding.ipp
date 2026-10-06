#pragma once
#include <cstdint>

namespace stl {
	// Legacy six-bit encoding, not encryption. Alphabet: 0x3c through 0x7b.
	LIBSTL_INLINE std::string Encrypt::MemadeEncode(const std::string& input) {
		if (input.empty())
			return {};
		std::string output(1, '#');
		std::uint32_t pending = 0;
		unsigned bits = 0;
		for (char ch : input) {
			pending = (pending << 8) | static_cast<unsigned char>(ch);
			bits += 8;
			while (bits >= 6) {
				bits -= 6;
				output.push_back(static_cast<char>(0x3c + ((pending >> bits) & 63u)));
			}
		}
		if (bits != 0)
			output.push_back(static_cast<char>(0x3c + ((pending << (6 - bits)) & 63u)));
		output.push_back('!');
		return output;
	}

	LIBSTL_INLINE std::string Encrypt::MemadeDecode(const std::string& input) {
		const auto end = input.rfind('!');
		if (input.empty() || input.front() != '#' || end == std::string::npos || end < 1)
			return {};
		// Preserve acceptance of a suffix following the closing marker.
		if ((end - 1) % 4 == 1)
			return {};
		std::string output;
		std::uint32_t pending = 0;
		unsigned bits = 0;
		for (std::size_t i = 1; i < end; ++i) {
			const auto ch = static_cast<unsigned char>(input[i]);
			if (ch < 0x3c || ch > 0x7b)
				return {};
			pending = (pending << 6) | (ch - 0x3cu);
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				output.push_back(static_cast<char>((pending >> bits) & 255u));
			}
		}
		if (bits != 0 && (pending & ((1u << bits) - 1)) != 0)
			return {};
		return output;
	}
} // namespace stl
