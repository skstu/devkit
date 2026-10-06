#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <libstl/config.hpp>

namespace stl {
	class MainProc final {
	public:
		using MainProcCb =
		    std::function<void(const std::string& input, bool& exit_flag)>;

	public:
		MainProc(const MainProcCb& callback);
		~MainProc() = default;

	private:
		void Proc();
		const MainProcCb callback_;
		MainProc(const MainProc&) = delete;
		MainProc& operator=(const MainProc&) = delete;
		void* operator new(size_t) = delete;
	};

} // namespace stl

#if LIBSTL_HEADER_ONLY
#include <libstl/detail/main_proc.ipp>
#endif
