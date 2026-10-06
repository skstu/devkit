#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <libpath/path.hpp>
#include <libstl/config.hpp>

namespace stl {
	using path = libpath::Path;

	class File final {
	public:
	};

	class String final {
	public:
	};
	class Utils final {
	public:
		template <typename T>
		static std::optional<T> StringToIntegerInterval(const std::string& str) {
			if constexpr (!std::is_integral_v<T>) {
				return std::nullopt;
			}
			else {
				std::string_view sv(str);
				// trim leading/trailing whitespace
				size_t start = 0, end = sv.size();
				while (start < end && std::isspace(static_cast<unsigned char>(sv[start])))
					++start;
				while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1])))
					--end;
				sv = sv.substr(start, end - start);

				if (sv.empty())
					return std::nullopt;

				if constexpr (std::is_signed_v<T>) {
					long long tmp = 0;
					auto res = std::from_chars(sv.data(), sv.data() + sv.size(), tmp);
					if (res.ec != std::errc() || res.ptr != sv.data() + sv.size())
						return std::nullopt;
					if (tmp < static_cast<long long>(std::numeric_limits<T>::min()) ||
					    tmp > static_cast<long long>(std::numeric_limits<T>::max()))
						return std::nullopt;
					return static_cast<T>(tmp);
				}
				else {
					unsigned long long tmp = 0;
					auto res = std::from_chars(sv.data(), sv.data() + sv.size(), tmp);
					if (res.ec != std::errc() || res.ptr != sv.data() + sv.size())
						return std::nullopt;
					if (tmp > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
						return std::nullopt;
					return static_cast<T>(tmp);
				}
			}
		}
		template <typename T>
		static T StringToInteger(const std::string& s, T def = {}) {
			auto v = StringToIntegerInterval<T>(s);
			return v ? *v : def;
		}
		static std::int32_t GetReqID();
		static bool ParseAddrUrl(const std::string&, std::string&, int&);
		static std::map<std::string, std::string> ParseCmdline(const std::string&,
		                                                       const bool&);
		static std::string BinaryToHexString(const std::string&);
		static std::string HexStringToBinary(const std::string&);
		template <typename T = std::u16string>
		    requires std::is_convertible_v<T, std::string> ||
		             std::is_convertible_v<T, std::wstring> ||
		             std::is_convertible_v<T, std::u8string> ||
		             std::is_convertible_v<T, std::u16string> ||
		             std::is_convertible_v<T, std::u32string>
		static bool CompareI(const T& a, const T& b) {
			return !ToLower<T>(a).compare(ToLower<T>(b));
		}
		static bool MakeFile(const path&);
		static bool MakeDirectory(const path&);
		static bool DirectoryExists(const stl::path&);
		static void EnumDirectory(const stl::path&, std::map<stl::path, stl::path>&,
		                          std::map<stl::path, stl::path>&, const bool&);
		static bool FileExists(const path&);
		static bool RemoveDir(const stl::path&);
		static bool RemoveFile(const stl::path&);
		static bool WriteFile(const stl::path&, const std::string&,
		                      const int& mode = static_cast<int>(std::ios::binary) |
		                                        static_cast<int>(std::ios::out) |
		                                        static_cast<int>(std::ios::trunc));
		static size_t FileSize(const path&);
		static std::string ReadFile(const stl::path&,
		                            const int& mode = std::ios::in |
		                                              std::ios::binary);
		static bool WriteFileAddto(const path&, const std::string&);
		template <typename T>
		static T ToLower(const T& input) {
			T result(input);
			// ASCII case folding preserves UTF-8 bytes and non-ASCII code units.
			for (auto& ch : result)
				if (ch >= 'A' && ch <= 'Z')
					ch = static_cast<typename T::value_type>(ch + ('a' - 'A'));
			return result;
		}
		template <typename T>
		static T ToUpper(const T& input) {
			T result(input);
			for (auto& ch : result)
				if (ch >= 'a' && ch <= 'z')
					ch = static_cast<typename T::value_type>(ch - ('a' - 'A'));
			return result;
		}
		template <typename T,
		          typename = std::enable_if_t<std::is_same_v<T, std::string> ||
		                                      std::is_same_v<T, std::wstring>>>
		static std::vector<T> StringSplit(const T& input, const T& delim) {
			std::vector<T> result;
			if (input.empty())
				return result;
			if (delim.empty()) {
				result.emplace_back(input);
				return result;
			}

			// Literal delimiter; preserve leading/interior empty fields, omit trailing one.
			typename T::size_type start = 0;
			while (start < input.size()) {
				const auto end = input.find(delim, start);
				result.emplace_back(input.substr(start, end - start));
				if (end == T::npos)
					break;
				start = end + delim.size();
			}

			return result;
		}
	};

} // namespace stl

#if LIBSTL_HEADER_ONLY
#include <libstl/detail/utils.ipp>
#endif
