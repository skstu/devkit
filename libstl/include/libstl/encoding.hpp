#pragma once

#include <string>
#include <libstl/config.hpp>

namespace stl {
	class Encrypt final {
	public:
		static std::string MemadeDecode(const std::string&);
		static std::string MemadeEncode(const std::string&);
	};

} // namespace stl

#if LIBSTL_HEADER_ONLY
#include <libstl/detail/encoding.ipp>
#endif
