#include "stdafx.h"

static std::map<std::string, ElementObject*> map_element_objs;

static bool IsValidPoint(const POINT& pt) {
	bool result = false;
	do {
		RECT rtDesktop;
		if (FALSE == GetWindowRect(GetDesktopWindow(), &rtDesktop))
			break;
		/*const size_t width = rtDesktop.right - rtDesktop.left;
		const size_t height = rtDesktop.bottom - rtDesktop.top;
		rtDesktop.left -= width * 10;
		rtDesktop.top -= height * 10;
		rtDesktop.right += width * 10;
		rtDesktop.bottom += height * 10;*/
		result = PtInRect(&rtDesktop, pt) == TRUE ? true : false;
	} while (0);
	return result;
}
static bool IsValidElementPos(const inspect::IElement* pElement) {
	bool result = false;
	do {
		if (!pElement)
			break;
		POINT pt;
		pt.x = pElement->CenterX();
		pt.y = pElement->CenterY();
		result = IsValidPoint(pt);
	} while (0);
	return result;
}


static void update_path(const std::string& path_deps) {
	// Get the current PATH environment variable
	char* old_path = std::getenv("PATH");
	if (old_path == nullptr) {
		std::cerr << "Failed to get PATH environment variable." << std::endl;
		return;
	}

	// Create a new PATH string
	std::string new_path = old_path;
	new_path.append(";");
	new_path.append(path_deps);

	// Update the PATH environment variable
	std::string new_env = std::format("PATH={}", new_path);
	int result = _putenv(new_env.c_str());
	if (result != 0) {
		std::cerr << "Failed to set PATH environment variable." << std::endl;
	}
	else {
		std::cout << "PATH environment variable updated successfully." << std::endl;
	}
}

Core::Core(const HMODULE& _m) : module_(_m) {
	Init();
}

Core::~Core() {
	UnInit();
}
void Core::Release() const {
	delete this;
}
bool Core::Ready() const {
	return ready_.load();
}
void Core::Init() {
	path_python_ = Global::GetModulePath(GetModuleHandleW(NULL));
	path_module_ = Global::GetModulePath(module_);
	do {
#ifdef _DEBUG
		pWin_ = win::IWin::Create(
			R"(C:\Users\k34ub\source\skstu\devkit\bin\projects\x64\Debug\win.dll)");
		pUiohook_ = uiohook::IUiohook::Create(
			R"(C:\Users\k34ub\source\skstu\devkit\out\uiohook.dll)");
		pAutomation_ = inspect::IAutomation::Create(
			R"(C:\Users\k34ub\source\skstu\devkit\out\automation.dll)");
#else
		pWin_ = win::IWin::Create("win.dll");
		pUiohook_ = uiohook::IUiohook::Create(R"(uiohook.dll)");
		pAutomation_ = inspect::IAutomation::Create(R"(automation.dll)");
#endif
		if (!pWin_ || !pUiohook_ || !pAutomation_)
			break;

#ifdef _DEBUG
		// path_data_ = R"(C:\Users\k34ub\source\skstu\devkit\out\data)";
		path_data_ =
			R"(C:\Users\k34ub\source\work\rpaWindowsUI\tools\python\inspection\data)";
#else
		path_data_ = path_python_ + "..\\inspection\\data\\";
#endif

		pAutomation_->Setup(path_data_.c_str());

		std::cout << path_python_ << std::endl;
		std::cout << path_module_ << std::endl;
		std::cout << path_data_ << std::endl;

		ready_.store(true);
	} while (0);
}

void Core::UnInit() {
	do {
		if (!ready_.load())
			break;

		for (auto& e : map_element_objs)
			e.second->Release();
		map_element_objs.clear();

		uiohook::IUiohook::Destroy(&pUiohook_);
		inspect::IAutomation::Destroy(&pAutomation_);
		win::IWin::Destroy(&pWin_);

		ready_.store(false);
	} while (0);
}
bool Core::Start() {
	do {
		if (open_.load())
			break;
		std::string strTmppath;
		strTmppath.resize(_MAX_PATH, 0x00);
		DWORD dwTmppath = ::GetTempPathA(_MAX_PATH, &strTmppath[0]);
		strTmppath.resize(dwTmppath);

		const std::string path_log =
			stl::Path::FixA(strTmppath + "\\inspection\\__capture_pyd__.log", "\\");
		LOG_INIT(path_log);

#ifdef _DEBUG
		std::vector<std::string> path_deps = {
			R"(C:\Users\k34ub\source\skstu\devkit\out\)",
			R"(C:\Users\k34ub\source\skstu\devkit\bin\projects\x64\Debug\)" };
#else
		std::vector<std::string> path_deps = { path_python_ + R"(\..\inspection)" };
#endif

		for (const auto& path : path_deps)
			update_path(path);

#ifndef _DEBUG
		if (!pUiohook_->Start())
			break;
#endif

		pAutomation_->Start();

		LoadData();
		open_.store(true);
	} while (0);
	return open_.load();
}

void Core::Stop() {
	do {
		if (!open_.load())
			break;
#ifndef _DEBUG
		pUiohook_->Stop();
#endif
		pAutomation_->Stop();

		open_.store(false);
	} while (0);
	// LOG_UNINIT;
}

ElementObject* Core::FindElement(const std::string& key, const WindowType& type) const {
	ElementObject* result = nullptr;
	std::lock_guard<std::mutex> lock{ *mutex_ };
	inspect::IElement* pFound = nullptr;

	switch (type) {
	case WindowType::WINDOW_TYPE_DESKTOP: {
		if (!key.empty()) {
			if (map_element_objs.empty())
				break;
			auto found = map_element_objs.find(key);
			if (found == map_element_objs.end())
				break;
			found->second->SetWindowType(type);
			//ActivateApp(reinterpret_cast<unsigned long long>(found->second));
			pFound =
				pAutomation_->FindElementFormDesktop(found->second->Element());
			if (!pFound) {
				std::cout << std::format("Element not found on key '{}'.", key) << std::endl;
				break;
			}
			found->second->Element(pFound);
			result = found->second;
			break;
		}
		HWND hDesktop = GetDesktopWindow();
		inspect::IElement* pEle = pAutomation_->GetElementFromHwnd(hDesktop);
		if (!pEle)
			break;
		auto exists = map_element_objs.find(pEle->Identify());
		if (exists != map_element_objs.end()) {
			exists->second->Release();
			map_element_objs.erase(exists);
		}
		result = new ElementObject();
		result->Element(pEle);
		map_element_objs.emplace(result->Key(), result);
	}break;
	case WindowType::WINDOW_TYPE_PROGRAM: {
		if (key.empty())
			break;
		if (map_element_objs.empty())
			break;
		auto found = map_element_objs.find(key);
		if (found == map_element_objs.end())
			break;
		found->second->SetWindowType(type);
		//ActivateApp(reinterpret_cast<unsigned long long>(found->second));
		pFound =
			pAutomation_->FindElementFormRoot(found->second->Element(), true);
		if (!pFound) {
			std::cout << std::format("Element not found on key '{}'.", key) << std::endl;
			break;
		}

		do {
			auto info = found->second->Wininfo();
			if (!info) {
				std::cout << fmt::format("Nof found 'window info' on ({}).", key) << std::endl;
				break;
			}
			if (!info->ImagePathname()) {
				std::cout << fmt::format("Nof found 'program image path' on ({}).", key) << std::endl;
				break;
			}
			char* path = nullptr;
			size_t path_size = 0;
			pWin_->WStringToMBytes(info->ImagePathname(), wcslen(info->ImagePathname()),
				&path, &path_size);
			std::string strPath(path, path_size);
			pWin_->FreePtr((void**)&path);
			unsigned long pid = 0;
			pWin_->ProcessCreate(strPath.c_str(), "", &pid, false, true, 3000);

			std::cout << fmt::format("Open process on ({}).", strPath) << std::endl;
		} while (0);

		HWND hFound = FindWindowW(pFound->ClassName(), pFound->Name());
		if (hFound) {
			ShowWindow(hFound, SW_SHOW);
			BringWindowToTop(hFound);
			SetForegroundWindow(hFound);
			SetWindowPos(hFound, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}


		found->second->Element(pFound);
		result = found->second;
	}break;
	default: {
	}break;
	}
	return result;
}

bool Core::SendText(const tfWindow& window, const std::string& key, const std::string& text) const {
	bool result = false;
	wchar_t* wstr = nullptr;
	size_t wstrLen = 0;
	do {
		if (text.empty() || key.empty() || window <= 0 || !pUiohook_)
			break;
		if (!pWin_)
			break;
		if (!pWin_->UTF8ToWString(text.data(), text.size(), &wstr, &wstrLen))
			break;
		ElementObject* wndObj = reinterpret_cast<ElementObject*>(window);
		ElementObject* foundObj = FindElement(key, wndObj->GetWindowType());
		if (!foundObj)
			break;
		pUiohook_->SendTextW(wstr, wstrLen, foundObj->Element()->CenterX(),
			foundObj->Element()->CenterY());
		result = true;
	} while (0);
	if (wstr) {
		pWin_->FreePtr((void**)&wstr);
	}
	return result;
}
bool Core::ShowDesktop() const {
	bool result = false;
	std::thread([]() {
		IShellDispatch4* pdisp = nullptr;
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		do {
			if (S_OK != hr)
				break;
			if (S_OK != ::CoCreateInstance(CLSID_Shell, NULL, CLSCTX_ALL,
				__uuidof(IShellDispatch4),
				(void**)&pdisp) ||
				!pdisp)
				break;
			if (S_OK != pdisp->ToggleDesktop())
				break;

		} while (0);

		SK_RELEASE_PTR(pdisp);
		CoUninitialize();
		}).join();
#if 0

		// Simulate pressing the Win key
		INPUT inputs[4] = {};

		// Press Win key
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = VK_LWIN;

		// Press D key
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = 'D';

		// Release D key
		inputs[2].type = INPUT_KEYBOARD;
		inputs[2].ki.wVk = 'D';
		inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

		// Release Win key
		inputs[3].type = INPUT_KEYBOARD;
		inputs[3].ki.wVk = VK_LWIN;
		inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

		// Send the inputs
		SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
#endif
		return result;
}
ElementObject* Core::GetRootElementObject(ElementObject* child) const {
	ElementObject* result = nullptr;
	do {
		if (!child || !child->Element())
			break;
		auto found = map_element_objs.find(child->Element()->Identify());
		if (found == map_element_objs.end())
			break;
		result = found->second;
	} while (0);
	return result;
}
void Core::ActivateApp(const tfWindow& windowObj) const {
	do {
		if (windowObj <= 0)
			break;
		if (!pWin_)
			break;
		auto pWndObj = reinterpret_cast<ElementObject*>(windowObj);
		if (!pWndObj)
			break;
		if (pWndObj->GetWindowType() != WindowType::WINDOW_TYPE_PROGRAM)
			break;
		if (!pWndObj->Element())
			break;
		auto hWnd = pWndObj->Element()->NativeWindowHandle();
		auto Pid = pWndObj->Element()->ProcessId();

		HANDLE hProcess = nullptr;
		hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, Pid);


		HWND hRoot = nullptr;
		/*FindWindowW(pRoot->Element()->ClassName(), pRoot->Element()->Name());*/
		if (!hRoot)
			break;
		ShowWindow(hRoot, SW_SHOW);
		BringWindowToTop(hRoot);
		SetForegroundWindow(hRoot);
		SetWindowPos(hRoot, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#if 0
		auto info = pItem->Wininfo();
		if (!info || !info->ImagePathname())
			break;
		char* path = nullptr;
		size_t path_size = 0;
		pWin_->WStringToMBytes(info->ImagePathname(), wcslen(info->ImagePathname()),
			&path, &path_size);
		std::string strPath(path, path_size);
		pWin_->FreePtr((void**)&path);
		unsigned long pid = 0;
		pWin_->ProcessCreate(strPath.c_str(), "", &pid, false, true, 3000);
#endif
	} while (0);
}


bool Core::MouseLeftDoubleClick(const tfWindow& window,
	const std::string& key) const {
	bool result = false;
	do {
		if (key.empty() || window <= 0 || !pUiohook_)
			break;
		ElementObject* wndObj = reinterpret_cast<ElementObject*>(window);
		ElementObject* foundObj = FindElement(key, wndObj->GetWindowType());
		if (!foundObj)
			break;
		pUiohook_->MouseClick(
			uiohook::UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT,
			foundObj->Element()->CenterX(), foundObj->Element()->CenterY(), 2);
		result = true;
	} while (0);
	return result;
}
bool Core::MouseMoveTo(const tfWindow& window, const std::string& key, const tfStep& step) const {
	bool result = false;
	do {
		if (key.empty() || window <= 0 || !pUiohook_)
			break;
		ElementObject* wndObj = reinterpret_cast<ElementObject*>(window);
		ElementObject* foundObj = FindElement(key, wndObj->GetWindowType());
		if (!foundObj)
			break;
		pUiohook_->MouseMoveTo(foundObj->Element()->CenterX(), foundObj->Element()->CenterY(), step);
		result = true;
	} while (0);
	return result;
}

bool Core::MouseLeftClick(const tfWindow& window,
	const std::string& key) const {
	bool result = false;
	do {
		if (key.empty() || window <= 0 || !pUiohook_)
			break;
		ElementObject* wndObj = reinterpret_cast<ElementObject*>(window);
		ElementObject* foundObj = FindElement(key, wndObj->GetWindowType());
		if (!foundObj)
			break;
		pUiohook_->MouseClick(
			uiohook::UioVirtualMouseButtons::UIO_MOUSE_BUTTON_LEFT,
			foundObj->Element()->CenterX(), foundObj->Element()->CenterY(), 1);
		result = true;
	} while (0);
	return result;
}

bool Core::MouseRightClick(const tfWindow& window, const std::string& key) const {
	bool result = false;
	do {
		if (key.empty() || window <= 0 || !pUiohook_)
			break;
		ElementObject* wndObj = reinterpret_cast<ElementObject*>(window);
		ElementObject* foundObj = FindElement(key, wndObj->GetWindowType());
		if (!foundObj)
			break;
		pUiohook_->MouseClick(
			uiohook::UioVirtualMouseButtons::UIO_MOUSE_BUTTON_RIGHT,
			foundObj->Element()->CenterX(), foundObj->Element()->CenterY(), 1);
		result = true;
	} while (0);
	return result;
}
#if 0
capture.op_begin()
desk = capture.op_find_capture(window_type = 1, key = None)
dd = capture.op_find_capture(
	window_type = 2, key = '77f502057da1d39c74e2424aeaa93f35')
	ret = capture.op_mouse_drag_item2item(
		window1 = desk, key1 = '9d6d1c57a998e7b71b44defed7448d64', window2 = dd, key2 = '468c6e0945ede688a14463d403def7f5')
	capture.op_end()
#endif
	void Core::Test() {

	ShowDesktop();
	path_data_ = R"(C:\Users\k34ub\Desktop\RPA\data)";
	LoadData();
	pAutomation_->Setup(path_data_.c_str());
	auto desk = FindElement("", WindowType::WINDOW_TYPE_DESKTOP);
	auto dd = FindElement("77f502057da1d39c74e2424aeaa93f35", WindowType::WINDOW_TYPE_PROGRAM);

	MouseDragged(reinterpret_cast<tfWindow>(desk), "bd81b323a06c1710c12e606899aeeee9", reinterpret_cast<tfWindow>(dd), "468c6e0945ede688a14463d403def7f5");
	auto sk = 0;
#if 0
	long t1_x = 1176;
	long t1_y = 54;
	long t2_x = 392;
	long t2_y = 54;
	//pUiohook_->MouseDragged(300, 300, 50, 50);
	pUiohook_->MouseDragged(t1_x, t1_y, t2_x, t2_y);
#endif
#if 0
	auto f1 = FindElement("43627b5837e9a2fe336a875dbccf1079", WindowType::WINDOW_TYPE_DESKTOP);
	auto f2 = FindElement("468c6e0945ede688a14463d403def7f5", WindowType::WINDOW_TYPE_PROGRAM);

	MouseDragged(reinterpret_cast<unsigned long long>(f1), "43627b5837e9a2fe336a875dbccf1079", reinterpret_cast<unsigned long long>(f2), "468c6e0945ede688a14463d403def7f5");
	auto sksks = 0;
#endif
}
bool Core::MouseDragged(const tfWindow& window1, const std::string& key1, const tfWindow& window2, const std::string& key2) const {
	bool result = false;
	do {
		if (key1.empty() || window1 <= 0 || key2.empty() || window2 <= 0 || !pUiohook_)
			break;
		ElementObject* wndObj1 = reinterpret_cast<ElementObject*>(window1);
		ElementObject* wndObj2 = reinterpret_cast<ElementObject*>(window2);

		ElementObject* foundObj1 = FindElement(key1, wndObj1->GetWindowType());
		ElementObject* foundObj2 = FindElement(key2, wndObj2->GetWindowType());

		if (!foundObj1 || !foundObj2)
			break;
		if (!IsValidElementPos(foundObj2->Element()) || !IsValidElementPos(foundObj1->Element()))
			break;

		pUiohook_->MouseDragged(
			foundObj2->Element()->CenterX(), foundObj2->Element()->CenterY(),
			foundObj1->Element()->CenterX(), foundObj1->Element()->CenterY());
		result = true;
	} while (0);
	return result;
}

void Core::SetDataDir(const std::string& data_dir) {
	path_data_ = data_dir;
	LoadData();
}
void Core::LoadData() {
	win::IDirectoryArray* pDirs = nullptr;
	do {
		if (!pWin_)
			break;
		pDirs = pWin_->EnumFolder(path_data_.c_str(), false);
		if (!pDirs)
			break;
		const auto total = pDirs->Total();
		for (size_t i = 0; i < pDirs->Total(); ++i) {
			win::IDirectoryElement* ele = pDirs->Next(i);
			if (!ele)
				continue;
			if (!ele->IsDir())
				continue;
			const std::string ele_key = ele->Name();
			win::IDirectoryArray* eleFileObjs = pWin_->EnumFolder(ele->Path(), false);
			do {
				if (!eleFileObjs)
					break;
				ElementObject* pEleobj = nullptr;
				for (size_t j = 0; j < eleFileObjs->Total(); ++j) {
					auto ele_f = eleFileObjs->Next(j);
					if (!ele_f)
						continue;
					if (ele_f->IsDir())
						continue;
					if (!pEleobj)
						pEleobj = new ElementObject();
					if (stricmp("ele.bin", ele_f->Name()) == 0) {
						std::string data = stl::File::ReadFile(ele_f->Path());
						inspect::IElement* pElement =
							pAutomation_->CreateElementFromBin(data.data(), data.size());
						pEleobj->Element(pElement);
					}
					else if (stricmp("pro.wnd", ele_f->Name()) == 0) {
						std::string file = stl::File::ReadFile(ele_f->Path());
						win::IWindowInfo* info = pWin_->CreateWindowInfo(file.c_str());
						if (info) {
							pEleobj->Prownd(file);
							pEleobj->Wininfo(info);
						}
					}
					else if (stricmp("ele.png", ele_f->Name()) == 0) {
						pEleobj->Elepng(stl::File::ReadFile(ele_f->Path()));
					}
				}
				if (pEleobj)
					map_element_objs.emplace(pEleobj->Key(), pEleobj);
			} while (0);
			SK_RELEASE_PTR(eleFileObjs);
		}
		auto sksks = 0;
	} while (0);
	SK_RELEASE_PTR(pDirs);
}

win::IWin* Core::Win() const {
	return pWin_;
}

Core* __gpCore = nullptr;
Core* CoreGet() {
	return __gpCore;
}
HINSTANCE __gpHinstance = nullptr;
