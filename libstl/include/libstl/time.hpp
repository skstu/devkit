#pragma once

#include <chrono>
#include <libstl/clock.hpp>
#include <ctime>
#include <string>
#include <libstl/config.hpp>

namespace stl {
	struct DateTime {
		int year = 0;
		int month = 0;
		int day = 0;
		int hour = 0;
		int minute = 0;
		int second = 0;
		int millisecond = 0;
		int microsecond = 0;
	};

	class Time final {
	public:
		template <typename T = std::chrono::seconds>
		static time_t TimeStamp() {
			return std::chrono::duration_cast<T>(
			           std::chrono::time_point_cast<T>(std::chrono::system_clock::now())
			               .time_since_epoch())
			    .count();
		}

		static DateTime GetDateTime();
		static std::string GetISO8601Time();
	};

} // namespace stl

#if LIBSTL_HEADER_ONLY
#include <libstl/detail/time.ipp>
#endif
