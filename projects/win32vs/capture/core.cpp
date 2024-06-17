#include "stdafx.h"

Core::Core(const HMODULE& _m) : module_(_m) {
	Init();
}

Core::~Core() {
	UnInit();
}
bool Core::Ready() const {
	return ready_.load();
}
void Core::Init() {
	path_python_ = Global::GetModulePath(GetModuleHandleW(NULL));
	path_ = Global::GetModulePath(module_);
#ifdef _DEBUG
	const std::string path_deps = R"(C:\Users\k34ub\source\skstu\devkit\out\)";
#else
	const std::string path_deps = path_python_ + R"(\..\inspection\)";
#endif
	const std::string path_log = path_ + "\\capture.log";
	LOG_INIT(path_log);
	do {
		std::string env_path = getenv("PATH");
		env_path.append(";");
		env_path.append(path_deps);
		int r = putenv(fmt::format("PATH={}", env_path).c_str());
		if (r != 0) {
			LOG_OUTPUT("putenv({}).", r);
			break;
		}
		//pInspection_ = inspect::Inspection::Create(R"(C:\Users\k34ub\source\skstu\devkit\out\inspection.dll)");
		//pInspection_->Start();
		pUiohook_ = uiohook::IUiohook::Create(R"(C:\Users\k34ub\source\skstu\devkit\out\uiohook.dll)");

		ready_.store(true);
	} while (0);
}

void Core::UnInit() {
	do {
		if (!ready_.load())
			break;

		uiohook::IUiohook::Destroy(&pUiohook_);
		//inspect::Inspection::Destroy(&pInspection_);
		ready_.store(false);
	} while (0);
	LOG_UNINIT;
}

bool Core::Start() {
	do {
		if (open_.load())
			break;
		if (!pUiohook_->Start())
			break;

		open_.store(true);
	} while (0);

	return open_.load();
}

void Core::Stop() {
	do {
		if (!open_.load())
			break;
		if (!pUiohook_)
			break;
		pUiohook_->Stop();
		open_.store(false);
	} while (0);
}




Core* __gpCore = nullptr;
Core* CoreGet() { return __gpCore; }
HINSTANCE __gpHinstance = nullptr;
