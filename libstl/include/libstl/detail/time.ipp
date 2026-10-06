#pragma once
#include <iomanip>
#include <sstream>

namespace stl {

	LIBSTL_INLINE std::string Time::GetISO8601Time() {
		const auto now = std::chrono::system_clock::now();
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		                    now.time_since_epoch()) %
		                1000;
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(std::chrono::floor<std::chrono::seconds>(now));
		std::tm tm{};
#if defined(_WIN32)
		if (gmtime_s(&tm, &nowTime) != 0)
			return {};
#else
		if (!gmtime_r(&nowTime, &tm))
			return {};
#endif
		std::ostringstream oss;
		oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
		    << std::setfill('0') << ms.count() << 'Z';
		return oss.str();
	}
	LIBSTL_INLINE DateTime Time::GetDateTime() {
		auto now = std::chrono::system_clock::now();
		auto us_total = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
		auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

		std::time_t tt = static_cast<std::time_t>(secs);
		std::tm tm{};
#if defined(_WIN32)
		if (localtime_s(&tm, &tt) != 0)
			return {};
#else
		if (!localtime_r(&tt, &tm))
			return {};
#endif

		int micros_remainder = static_cast<int>(us_total % 1'000'000); // 0..999999
		int millis = micros_remainder / 1000;                          // 0..999
		int micros = micros_remainder % 1000;                          // 0..999

		return DateTime{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		                tm.tm_min, tm.tm_sec, millis, micros};
	}
} // namespace stl
