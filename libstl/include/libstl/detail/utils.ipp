#include <libstl/hex.hpp>
#pragma once
#include <chrono>
#include <ctime>
#include <regex>
#include <stdexcept>

namespace stl {

	LIBSTL_INLINE std::int32_t Utils::GetReqID() {
		auto now = std::chrono::system_clock::now();
		auto us_total = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
		auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

		std::time_t tt = static_cast<std::time_t>(secs);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &tt);
#else
		localtime_r(&tt, &tm);
#endif

		// byte0: year+month+day+hour+minute+second (mod 256)
		int sum = (tm.tm_year + 1900) + (tm.tm_mon + 1) + tm.tm_mday + tm.tm_hour +
		          tm.tm_min + tm.tm_sec;
		uint8_t b0 = static_cast<uint8_t>(sum & 0xFF);

		// bytes 1..3: std::chrono::microseconds within the second as 24-bit value (0..999999)
		uint32_t us_within_sec =
		    static_cast<uint32_t>(us_total % 1'000'000); // 0..999999
		uint8_t b1 = static_cast<uint8_t>((us_within_sec >> 16) & 0xFF);
		uint8_t b2 = static_cast<uint8_t>((us_within_sec >> 8) & 0xFF);
		uint8_t b3 = static_cast<uint8_t>(us_within_sec & 0xFF);

		uint32_t packed =
		    (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) |
		    (static_cast<uint32_t>(b2) << 8) | static_cast<uint32_t>(b3);

		// ensure >= 0xFF
		if (packed < 0xFF)
			packed |= 0xFFu;

		// keep positive int32
		return static_cast<std::int32_t>(packed & 0x7FFFFFFFu);
	}

	LIBSTL_INLINE bool Utils::ParseAddrUrl(const std::string& url, std::string& host, int& port) {
		static const std::regex re(
		    R"(^(?:(?:tcp|ws|wss|http|https)://)?(\[[0-9A-Fa-f:]+\]|[^:/\s]+):(\d+)(?:[/?#].*)?$)",
		    std::regex::ECMAScript | std::regex::icase);
		std::smatch match;
		if (!std::regex_match(url, match, re))
			return false;
		const auto parsed = StringToIntegerInterval<int>(match[2].str());
		if (!parsed || *parsed <= 0 || *parsed > 65535)
			return false;
		std::string parsed_host = match[1].str();
		if (parsed_host.front() == '[' && parsed_host.back() == ']')
			parsed_host = parsed_host.substr(1, parsed_host.size() - 2);
		host = std::move(parsed_host);
		port = *parsed;
		return true;
	}
	LIBSTL_INLINE std::map<std::string, std::string>
	Utils::ParseCmdline(const std::string& cmdlineString,
	                    const bool& remove_key_flag) {
		std::map<std::string, std::string> result;
		do {
			if (cmdlineString.empty())
				break;
			std::vector<std::string> parsers =
			    StringSplit<std::string>(cmdlineString, " ");
			if (parsers.empty())
				break;
			for (size_t i = 0; i < parsers.size(); ++i) {
				if (parsers[i].empty())
					continue;
				std::string key, value;
				size_t equal = parsers[i].find('=');
				if (*parsers[i].begin() == '/') {
					key = parsers[i].substr(1, equal - 1);
				}
				else if (parsers[i].size() > 1 &&
				         (parsers[i][0] == '-' && parsers[i][1] == '-')) {
					if (remove_key_flag)
						key = parsers[i].substr(2, equal - 2);
					else
						key = parsers[i].substr(0, equal);
				}
				else if (i == 0) {
					key = parsers[i];
				}
				else {
					continue;
				}
				if (key.empty())
					continue;
				if (equal != std::string::npos) {
					value = parsers[i].substr(equal + 1, parsers[i].size());
				}
				result.emplace(key, value);
			}
		} while (0);
		return result;
	}

	LIBSTL_INLINE std::string Utils::BinaryToHexString(const std::string& input) {
        return stl::Hex({reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
    }
    LIBSTL_INLINE std::string Utils::HexStringToBinary(const std::string& input) {
        if (input.size() % 2)
            return {};
        std::string result(input.size() / 2, '\0');
        return stl::Unhex(input, {reinterpret_cast<std::uint8_t*>(result.data()), result.size()}, stl::HexCase::Either) ? result : std::string{};
    }
    LIBSTL_INLINE bool Utils::MakeFile(const path& path) {
		std::error_code ec;
		if (path.has_parent_path()) {
			libpath::CreateDirectories(path.parent_path(), &ec);
			if (ec)
				return false;
		}
		std::ofstream ofs =
		    libpath::OpenOutput(path, std::ios::binary | std::ios::out |
		                                  std::ios::trunc);
		if (!ofs) {
			ec = std::make_error_code(std::errc::io_error);
			return false;
		}
		return true;
	}
	LIBSTL_INLINE bool Utils::MakeDirectory(const path& path) {
		bool result = false;
		std::error_code ec;
		do {
			if (path.empty())
				break;
			try {
				if (libpath::Exists(path, &ec)) {
					result = libpath::IsDirectory(path, &ec) && !ec;
					break;
				}
				if (ec)
					break;
				result = libpath::CreateDirectories(path.lexically_normal(), &ec);
			}
			catch (...) {
				result = false;
			}
		} while (0);
		return result;
	}
	LIBSTL_INLINE bool Utils::DirectoryExists(const stl::path& input_path) {
		return libpath::IsDirectory(input_path);
	}
	LIBSTL_INLINE bool Utils::FileExists(const path& input_pathname) {
		if (input_pathname.empty())
			return false;
		return libpath::IsRegularFile(input_pathname);
	}

	LIBSTL_INLINE bool Utils::RemoveDir(const stl::path& dir) {
		bool result = false;
		try {
			do {
				if (!libpath::IsDirectory(dir))
					break;
				result = libpath::RemoveAll(dir) > 0;
			} while (0);
		}
		catch (...) {
			result = false;
		}
		return result;
	}
	LIBSTL_INLINE bool Utils::RemoveFile(const stl::path& file) {
		bool result = false;
		try {
			do {
				if (file.empty())
					break;
				if (libpath::IsDirectory(file))
					break;
				result = libpath::Remove(file);
			} while (0);
		}
		catch (...) {
			result = false;
		}
		return result;
	}

	LIBSTL_INLINE void Utils::EnumDirectory(const stl::path& input_path,
	                                        std::map<stl::path, stl::path>& dirs,
	                                        std::map<stl::path, stl::path>& files,
	                                        const bool& recursive) {
		if (!libpath::IsDirectory(input_path))
			return;

		if (recursive) {
			for (const auto& entry :
			     libpath::RecursiveDirectoryIterator(input_path)) {
				stl::path relative_path =
				    libpath::Relative(entry.path(), input_path);
				stl::path full_path =
				    (stl::path(input_path) / relative_path).lexically_normal();
				if (entry.is_directory()) {
					dirs.emplace(relative_path, full_path);
				}
				else {
					files.emplace(relative_path, full_path);
				}
			}
		}
		else {
			for (const auto& entry : libpath::DirectoryIterator(input_path)) {
				stl::path relative_path =
				    libpath::Relative(entry.path(), input_path);
				stl::path full_path =
				    (stl::path(input_path) / relative_path).lexically_normal();
				if (entry.is_directory()) {
					dirs.emplace(relative_path, full_path);
				}
				else {
					files.emplace(relative_path, full_path);
				}
			}
		}
	}

	LIBSTL_INLINE bool Utils::WriteFile(const stl::path& file, const std::string& data,
	                                    const int& mode) {
		if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
			return false;
		std::ofstream of =
		    libpath::OpenOutput(file,
		                        static_cast<std::ios_base::openmode>(mode));
		if (!of.is_open())
			return false;
		of.write(data.data(), static_cast<std::streamsize>(data.size()));
		if (!of.good()) {
			of.close();
			return false;
		}
		of.close();
		return of.good();
	}
	LIBSTL_INLINE std::string Utils::ReadFile(const stl::path& file, const int& mode) {
		std::string result;
		std::ifstream of =
		    libpath::OpenInput(file,
		                       static_cast<std::ios_base::openmode>(mode));
		if (!of.is_open())
			return result;
		// Read in chunks: no failed tellg() cast, no Windows long truncation,
		// and text mode CRLF conversion cannot leave zero padding in the result.
		char buffer[64 * 1024];
		while (of.read(buffer, sizeof(buffer)) || of.gcount() > 0)
			result.append(buffer, static_cast<std::size_t>(of.gcount()));
		if (of.bad() || !of.eof())
			return {};
		return result;
	}
	LIBSTL_INLINE bool Utils::WriteFileAddto(const path& file, const std::string& data) {
		return Utils::WriteFile(file, data,
		                        static_cast<int>(std::ios::binary) |
		                            static_cast<int>(std::ios::out) |
		                            static_cast<int>(std::ios::app));
	}

} // namespace stl
#include <libstl/detail/file.ipp>
