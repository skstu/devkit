#pragma once
namespace stl {
	LIBSTL_INLINE std::size_t Utils::FileSize(const path& file) {
		std::error_code error;
		const auto size = libpath::FileSize(file, &error);
		return error || size > std::numeric_limits<std::size_t>::max()
		           ? 0
		           : static_cast<std::size_t>(size);
	}
} // namespace stl
