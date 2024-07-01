#include "stdafx.h"
#include <rapidxml.h>
#include <rapidjson.h>

win::IWin* gpWin = nullptr;
inspect::IOverlay* gpOverlay = nullptr;
uiohook::IUiohook* gpUiohook = nullptr;
inspect::IAutomation* gpAutomation = nullptr;
int main(int argc, char* argv[]) {
	gpWin = win::IWin::Create(R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\win32.dll)");
	gpUiohook = uiohook::IUiohook::Create(R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\input.dll)");
	gpOverlay = inspect::IOverlay::Create(R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\overlay.dll)");
	gpAutomation = inspect::IAutomation::Create(R"(C:\Users\k34ub\source\skstu\devkit_last\bin\x64\Debug\automation.dll)");



	std::string input;
	do {
		input.clear();
		std::getline(std::cin, input);
		if (input == "q" || std::cin.eof()) {
			break;
		}
	} while (1);
	win::IWin::Destroy(&gpWin);
	uiohook::IUiohook::Destroy(&gpUiohook);
	inspect::IOverlay::Destroy(&gpOverlay);
	inspect::IAutomation::Destroy(&gpAutomation);
	return 0;
}