#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <libstl/config.hpp>

namespace stl {
	class Packet final {
	public:
		using Key = uint32_t;
		using Value = std::string;
		using Map = std::map<Packet::Key, Packet::Value>;

	public:
		Packet() = default;
		~Packet() = default;
		void* operator new(size_t) = delete;
		void operator delete(void*) = delete;
		Packet(const Packet&) = delete;
		Packet& operator=(const Packet&) = delete;

	public:
		bool Made(const std::map<Packet::Key, Packet::Value>&, std::string&) const;
		bool UnMade(const std::string&,
		            std::map<Packet::Key, Packet::Value>&) const;
	};

} // namespace stl

#if LIBSTL_HEADER_ONLY
#include <libstl/detail/packet.ipp>
#endif
