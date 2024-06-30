#ifndef CAPTURE_GLOBAL_H
#define CAPTURE_GLOBAL_H

typedef enum class WindowType : int {
	WINDOW_TYPE_UNKNOWN = 0,
	WINDOW_TYPE_DESKTOP = 1,
	WINDOW_TYPE_PROGRAM = 2,

	WINDOW_TYPE_BEGIN = WINDOW_TYPE_DESKTOP,
	WINDOW_TYPE_END = WINDOW_TYPE_PROGRAM,
}tfWindowType;

using tfWindow = unsigned long long;
using tfStep = unsigned int;

class Global {
public:
	static std::string GetModulePath(const HINSTANCE& hModule);
};

#endif //CAPTURE_GLOBAL_H
