#include "stdafx.h"

std::string Global::GetModulePath(const HINSTANCE& hModule) {
	std::string result;
	do {
		char Filename[MAX_PATH] = { 0 };
		if (::GetModuleFileNameA(hModule, Filename, MAX_PATH) <= 0)
			break;
		if (FALSE == ::PathRemoveFileSpecA(Filename))
			break;
		result = Filename;
		auto end = std::prev(result.end());
		if (*end != '\\' && *end != '/')
			result.push_back('\\');
	} while (0);
	return result;
}