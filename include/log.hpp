#include "stl.h"

#ifndef __RUNTIMES_LOG_H
#define __RUNTIME_LOG_H


enum class LogOutputType : std::uint64_t {
	OutputConsole = 1,
	OutputFile = OutputConsole << 1,
};

class Log final {
public:
	Log(const std::string& outputFilePathname = "")
		: m_OutputFilePathname(outputFilePathname) {

		if (!outputFilePathname.empty()) {
			m_OutputType = static_cast<std::uint64_t>(LogOutputType::OutputFile);
		}

		m_Mutex = std::make_shared<std::mutex>();
		m_IsOpen.store(true);
		m_ProcessThread = std::thread(
			[this]() {
				do {
					Output();

					if (Empty())
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
					if (!m_IsOpen.load())
						break;
				} while (1);
			});
	}
	~Log() {
		m_IsOpen.store(false);
		if (m_ProcessThread.joinable())
			m_ProcessThread.join();
	}
public:
	void operator<<(const std::string& log_data) {
		std::unique_lock<std::mutex> lock{ *m_Mutex, std::defer_lock };
		lock.lock();
		m_OutputQ.push(log_data);
		lock.unlock();
	}
	bool Empty() const {
		bool result = false;
		std::unique_lock<std::mutex> lock{ *m_Mutex, std::defer_lock };
		lock.lock();
		result = m_OutputQ.empty();
		lock.unlock();
		return result;
	}
private:
	void Output() {
		std::unique_lock<std::mutex> lock{ *m_Mutex, std::defer_lock };
		lock.lock();
		do {
			if (m_OutputQ.empty())
				break;
			std::string data = m_OutputQ.front();
			SYSTEMTIME stime = { 0 };
			::GetLocalTime(&stime);
			data.insert(0, std::format("{:04}/{:02}/{:02} {:02}:{:02}:{:02}/{:03}\t",
				stime.wYear, stime.wMonth, stime.wDay,
				stime.wHour, stime.wMinute, stime.wSecond, stime.wMilliseconds));
			data.append("\r\n");
			if (m_OutputType & static_cast<std::uint64_t>(LogOutputType::OutputConsole))
				std::cout << data << std::endl;
			if (m_OutputType & static_cast<std::uint64_t>(LogOutputType::OutputFile)) {
				if (!m_OutputFilePathname.empty())
					stl::File::WriteFileAddto(m_OutputFilePathname, data);
			}


			m_OutputQ.pop();
		} while (0);
		lock.unlock();
	}
private:
	const std::string m_OutputFilePathname;
	std::uint64_t m_OutputType = static_cast<std::uint64_t>(LogOutputType::OutputConsole);
	std::shared_ptr<std::mutex> m_Mutex;
	std::atomic_bool m_IsOpen = false;
	std::thread m_ProcessThread;
	std::queue<std::string> m_OutputQ;
};




static Log* __gspLog = nullptr;

/*
_EXPORT_STD using format_args  = basic_format_args<format_context>;
_EXPORT_STD using wformat_args = basic_format_args<wformat_context>;
*/
/*static void LOG_OUTPUT(const std::string_view _Fmt, const std::basic_format_args<std::format_context> _Args) {
	do {
		if (!__gspLog)
			break;
		std::string _Str;
		_Str.reserve(_Fmt.size() + _Args._Estimate_required_capacity());
		_STD vformat_to(std::back_insert_iterator{ _Str }, _Fmt, _Args);
		if (_Str.empty())
			break;
		*__gspLog << _Str;
	} while (0);
}*/

static void LOG_OUTPUT(const std::string& data) {
	do {
		if (!__gspLog)
			break;
		if (data.empty())
			break;
		*__gspLog << data;
	} while (0);
}
#if 0
template <class... _Types>
static void LOG_OUTPUT(const _STD _Fmt_string<_Types...> _Fmt, _Types&&... _Args) {
	do {
		if (!__gspLog)
			break;
		_STD string s = _STD vformat(_Fmt._Str, _STD make_format_args(_Args...));
		if (s.empty())
			break;
		*__gspLog << s;
	} while (0);
}

template <class... _Types>
static void LOG_MSGBOX(const _STD _Fmt_string<_Types...> _Fmt, _Types&&... _Args) {
	_STD string s = _STD vformat(_Fmt._Str, _STD make_format_args(_Args...));
	::MessageBoxA(NULL, s.c_str(), __FUNCTION__, MB_TOPMOST);
}
template <class... _Types>
static void LOG_MSGBOX(const _STD _Fmt_wstring<_Types...> _Fmt, _Types&&... _Args) {
	_STD wstring s = _STD vformat(_Fmt._Str, _STD make_wformat_args(_Args...));
	::MessageBoxW(NULL, s.c_str(), __FUNCTIONW__, MB_TOPMOST);
}

#if 0//!@ Not supported at present
template <class... _Types>
static void LOG_OUTPUT(const _STD wformat_string<_Types...> _Fmt, _Types&&... _Args) {
	do {
		_STD wstring s = _STD vformat(_Fmt.get(), _STD make_wformat_args(_Args...));
		if (s.empty())
			break;
		*__gspLog << s;
	} while (0);
}
#endif
#endif

#define LOG_INIT(strFilename) \
do{\
if (!__gspLog){\
__gspLog = new Log(strFilename);\
*__gspLog << "Init log module.";\
}\
} while (0);

#define LOG_UNINIT \
do{\
if(__gspLog){\
*__gspLog << "Uninit log module.";\
delete __gspLog;\
__gspLog = nullptr;\
}\
} while (0);\

#if 0


#define LOG_OUTPUT(s) \
do{\
if (!__gspLog)\
break;\
if (s.empty())\
break;\
*__gspLog << s;\
} while (0);\

#endif





/// /*_ Memade®（新生™） _**/
/// /*_ Thu, 27 Apr 2023 08:29:20 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
//#endif///__B4907D76_FA9F_49AD_AC90_62EB586B8F99__

#endif
