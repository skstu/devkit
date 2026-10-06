#if !defined(__D8DD65DA_7A7B_40C2_BD9F_62C7E9858622__)
#define __D8DD65DA_7A7B_40C2_BD9F_62C7E9858622__

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <iconv.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#ifdef GetObject
#undef GetObject
#endif
#endif

namespace libpath {

	using Path = std::filesystem::path;

	namespace detail {

		inline std::string ConvertEncoding(std::string_view input,
		                                   const char* from_encoding,
		                                   const char* to_encoding, bool* ok = nullptr) {
			if (ok)
				*ok = false;
			if (!from_encoding || !to_encoding)
				return {};
			iconv_t converter = iconv_open(to_encoding, from_encoding);
			if (converter == reinterpret_cast<iconv_t>(-1))
				return {};
			std::vector<char> output(input.size() * 4 + 32);
			char* source = const_cast<char*>(input.data());
			std::size_t source_size = input.size();
			char* destination = output.data();
			std::size_t destination_size = output.size();
			while (iconv(converter, &source, &source_size, &destination, &destination_size) ==
			       static_cast<std::size_t>(-1)) {
				if (errno != E2BIG) {
					iconv_close(converter);
					return {};
				}
				const auto written = static_cast<std::size_t>(destination - output.data());
				output.resize(output.size() * 2);
				destination = output.data() + written;
				destination_size = output.size() - written;
			}
			iconv_close(converter);
			if (ok)
				*ok = true;
			return std::string(output.data(), static_cast<std::size_t>(destination - output.data()));
		}

#if defined(_WIN32)
		inline std::wstring Utf8ToWide(std::string_view value, bool* ok = nullptr) {
			if (ok)
				*ok = true;
			if (value.empty())
				return {};
			if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
				if (ok)
					*ok = false;
				return {};
			}

			const int len = static_cast<int>(value.size());
			int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
			                                 len, nullptr, 0);
			DWORD flags = MB_ERR_INVALID_CHARS;
			if (needed <= 0) {
				flags = 0;
				needed = MultiByteToWideChar(CP_UTF8, flags, value.data(), len, nullptr, 0);
			}
			if (needed <= 0) {
				if (ok)
					*ok = false;
				return {};
			}

			std::wstring wide(static_cast<std::size_t>(needed), L'\0');
			const int written =
			    MultiByteToWideChar(CP_UTF8, flags, value.data(), len, wide.data(), needed);
			if (written <= 0) {
				if (ok)
					*ok = false;
				return {};
			}
			wide.resize(static_cast<std::size_t>(written));
			return wide;
		}

		inline std::string WideToUtf8(std::wstring_view value, bool* ok = nullptr) {
			if (ok)
				*ok = true;
			if (value.empty())
				return {};
			if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
				if (ok)
					*ok = false;
				return {};
			}

			const int len = static_cast<int>(value.size());
			const int needed =
			    WideCharToMultiByte(CP_UTF8, 0, value.data(), len, nullptr, 0, nullptr,
			                        nullptr);
			if (needed <= 0) {
				if (ok)
					*ok = false;
				return {};
			}

			std::string utf8(static_cast<std::size_t>(needed), '\0');
			const int written = WideCharToMultiByte(CP_UTF8, 0, value.data(), len,
			                                        utf8.data(), needed, nullptr, nullptr);
			if (written <= 0) {
				if (ok)
					*ok = false;
				return {};
			}
			utf8.resize(static_cast<std::size_t>(written));
			return utf8;
		}
#else
		inline std::string Utf8Bytes(std::string_view value) {
			return std::string(value.data(), value.size());
		}

		inline std::u16string Utf8ToUtf16(std::string_view value, bool* ok = nullptr) {
			if (ok)
				*ok = true;

			std::u16string out;
			out.reserve(value.size());
			for (std::size_t i = 0; i < value.size();) {
				const auto lead = static_cast<unsigned char>(value[i]);
				std::uint32_t codepoint = 0;
				std::size_t extra = 0;
				std::uint32_t minimum = 0;

				if (lead < 0x80) {
					codepoint = lead;
					extra = 0;
					minimum = 0;
				}
				else if ((lead & 0xE0) == 0xC0) {
					codepoint = lead & 0x1F;
					extra = 1;
					minimum = 0x80;
				}
				else if ((lead & 0xF0) == 0xE0) {
					codepoint = lead & 0x0F;
					extra = 2;
					minimum = 0x800;
				}
				else if ((lead & 0xF8) == 0xF0) {
					codepoint = lead & 0x07;
					extra = 3;
					minimum = 0x10000;
				}
				else {
					if (ok)
						*ok = false;
					return {};
				}

				if (i + extra >= value.size()) {
					if (ok)
						*ok = false;
					return {};
				}

				for (std::size_t j = 1; j <= extra; ++j) {
					const auto trail = static_cast<unsigned char>(value[i + j]);
					if ((trail & 0xC0) != 0x80) {
						if (ok)
							*ok = false;
						return {};
					}
					codepoint = (codepoint << 6) | (trail & 0x3F);
				}

				if (codepoint < minimum || codepoint > 0x10FFFF ||
				    (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
					if (ok)
						*ok = false;
					return {};
				}

				if (codepoint <= 0xFFFF) {
					out.push_back(static_cast<char16_t>(codepoint));
				}
				else {
					codepoint -= 0x10000;
					out.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
					out.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
				}
				i += extra + 1;
			}
			return out;
		}
#endif

	} // namespace detail

	inline Path FromUtf8(std::string_view value, bool* ok = nullptr) {
		if (ok)
			*ok = true;
#if defined(_WIN32)
		bool converted = false;
		std::wstring wide = detail::Utf8ToWide(value, &converted);
		if (ok)
			*ok = converted;
		if (!converted)
			return {};
		return Path(std::move(wide));
#else
		return Path(detail::Utf8Bytes(value));
#endif
	}

	inline Path FromUtf8(const std::string& value, bool* ok = nullptr) {
		return FromUtf8(std::string_view(value.data(), value.size()), ok);
	}

	inline Path FromUtf8(const char* value, bool* ok = nullptr) {
		return value ? FromUtf8(std::string_view(value), ok) : FromUtf8({}, ok);
	}

	inline std::string ToUtf8(const Path& value, bool* ok);

	// Converts explicit legacy encodings such as GB18030, Shift_JIS, EUC-KR or
	// Windows-1252 to the library's UTF-8 path representation. Prefer this over
	// guessing an encoding from raw bytes.
	inline Path FromEncoded(std::string_view value, const char* encoding,
	                        bool* ok = nullptr) {
		bool converted = false;
		const std::string utf8 = detail::ConvertEncoding(value, encoding, "UTF-8", &converted);
		if (ok)
			*ok = converted;
		return converted ? FromUtf8(utf8, ok) : Path();
	}

	inline std::string ToEncoded(const Path& value, const char* encoding,
	                             bool* ok = nullptr) {
		bool converted = false;
		const std::string result = detail::ConvertEncoding(ToUtf8(value, &converted), "UTF-8",
		                                                   encoding, &converted);
		if (ok)
			*ok = converted;
		return converted ? result : std::string();
	}

	inline std::string ToUtf8(const Path& value, bool* ok = nullptr) {
		if (ok)
			*ok = true;
#if defined(_WIN32)
		return detail::WideToUtf8(value.native(), ok);
#else
		const auto native = value.native();
		return std::string(native.data(), native.size());
#endif
	}

	inline std::string ToUtf8Generic(const Path& value, bool* ok = nullptr) {
		std::string result = ToUtf8(value, ok);
		if (ok && !*ok)
			return {};
		std::replace(result.begin(), result.end(), '\\', '/');
		return result;
	}

	inline std::string NativeString(const Path& value, bool* ok = nullptr) {
#if defined(_WIN32)
		return ToUtf8(value, ok);
#else
		if (ok)
			*ok = true;
		const auto native = value.native();
		return std::string(native.data(), native.size());
#endif
	}

#if defined(_WIN32)
	inline std::wstring ToWide(const Path& value) {
		return value.native();
	}

	inline std::wstring NativeWide(const Path& value) {
		return ToWide(value);
	}
#endif

	inline std::u16string ToUtf16(const Path& value, bool* ok = nullptr) {
		if (ok)
			*ok = true;
#if defined(_WIN32)
		const std::wstring native = value.native();
		std::u16string result;
		result.reserve(native.size());
		for (wchar_t ch : native)
			result.push_back(static_cast<char16_t>(ch));
		return result;
#else
		return detail::Utf8ToUtf16(ToUtf8(value, ok), ok);
#endif
	}

	inline Path LexicallyNormalUtf8(std::string_view value, bool* ok = nullptr) {
		bool converted = false;
		Path path = FromUtf8(value, &converted);
		if (ok)
			*ok = converted;
		return converted ? path.lexically_normal() : Path();
	}

	inline bool CreateDirectories(const Path& value,
	                              std::error_code* error_out = nullptr) {
		std::error_code ec;
		std::filesystem::create_directories(value, ec);
		if (error_out)
			*error_out = ec;
		return !ec;
	}

	inline bool Exists(const Path& value, std::error_code* error_out = nullptr) {
		std::error_code ec;
		const bool result = std::filesystem::exists(value, ec);
		if (error_out)
			*error_out = ec;
		return !ec && result;
	}

	inline bool IsRegularFile(const Path& value,
	                          std::error_code* error_out = nullptr) {
		std::error_code ec;
		const bool result = std::filesystem::is_regular_file(value, ec);
		if (error_out)
			*error_out = ec;
		return !ec && result;
	}

	inline bool IsDirectory(const Path& value, std::error_code* error_out = nullptr) {
		std::error_code ec;
		const bool result = std::filesystem::is_directory(value, ec);
		if (error_out)
			*error_out = ec;
		return !ec && result;
	}

	using DirectoryIterator = std::filesystem::directory_iterator;
	using RecursiveDirectoryIterator = std::filesystem::recursive_directory_iterator;
	using FileStatus = std::filesystem::file_status;
	using FilesystemError = std::filesystem::filesystem_error;
	using Perms = std::filesystem::perms;

	inline FileStatus Status(const Path& value,
	                         std::error_code* error_out = nullptr) {
		std::error_code ec;
		FileStatus status = std::filesystem::status(value, ec);
		if (error_out)
			*error_out = ec;
		return status;
	}

	inline bool IsRegularFileStatus(const FileStatus& status) {
		return std::filesystem::is_regular_file(status);
	}

	inline Path Relative(const Path& value, const Path& base,
	                     std::error_code* error_out = nullptr) {
		std::error_code ec;
		Path result = std::filesystem::relative(value, base, ec);
		if (error_out)
			*error_out = ec;
		return ec ? Path() : result;
	}

	inline std::uintmax_t FileSize(const Path& value,
	                               std::error_code* error_out = nullptr) {
		std::error_code ec;
		const std::uintmax_t result = std::filesystem::file_size(value, ec);
		if (error_out)
			*error_out = ec;
		return ec ? 0 : result;
	}

	inline bool Remove(const Path& value, std::error_code* error_out = nullptr) {
		std::error_code ec;
		const bool result = std::filesystem::remove(value, ec);
		if (error_out)
			*error_out = ec;
		return !ec && result;
	}

	inline std::uintmax_t RemoveAll(const Path& value,
	                                std::error_code* error_out = nullptr) {
		std::error_code ec;
		const std::uintmax_t result = std::filesystem::remove_all(value, ec);
		if (error_out)
			*error_out = ec;
		return ec ? 0 : result;
	}

	inline bool Rename(const Path& from, const Path& to,
	                   std::error_code* error_out = nullptr) {
		std::error_code ec;
		std::filesystem::rename(from, to, ec);
		if (error_out)
			*error_out = ec;
		return !ec;
	}

	inline std::ifstream OpenInput(const Path& value,
	                               std::ios::openmode mode = std::ios::binary) {
		return std::ifstream(value, mode);
	}

	inline std::fstream OpenIO(
	    const Path& value,
	    std::ios::openmode mode = std::ios::in | std::ios::out |
	                              std::ios::binary) {
		return std::fstream(value, mode);
	}

	inline std::ofstream OpenOutput(
	    const Path& value,
	    std::ios::openmode mode = std::ios::binary | std::ios::trunc) {
		return std::ofstream(value, mode);
	}

} // namespace libpath

/// /*_ Memade®（新生™） _**/
/// /*_ Header-only UTF-8/native path bridge _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__D8DD65DA_7A7B_40C2_BD9F_62C7E9858622__
