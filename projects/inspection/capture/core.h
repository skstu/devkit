#ifndef CAPTURE_CAPTURE_H
#define CAPTURE_CAPTURE_H

class Core {
public:
	Core(const HMODULE&);
	void Release() const;
private:
	~Core();
public:
	bool Ready() const;
	win::IWin* Win() const;
private:
	void Init();
	void UnInit();
public:
	bool Start();
	void Stop();
	ElementObject* FindElement(const std::string& key,const WindowType& = WindowType::WINDOW_TYPE_UNKNOWN) const;
	bool MouseLeftClick(const tfWindow& window, const std::string& key) const;
	bool MouseRightClick(const tfWindow& window, const std::string& key) const;
	bool MouseLeftDoubleClick(const tfWindow& window, const std::string& key) const;
	bool MouseMoveTo(const tfWindow& window, const std::string& key, const tfStep& step) const;
	bool MouseDragged(const tfWindow& window1, const std::string& key1, const tfWindow& window2, const std::string& key2) const;
	bool SendText(const tfWindow& window, const std::string& key, const std::string& text) const;

	void ActivateApp(const tfWindow& item) const;

	bool ShowDesktop() const;
	void SetDataDir(const std::string&);
private:
	ElementObject* GetRootElementObject(ElementObject* child) const;
public:
	void LoadData();
	void Test();
private://!@ deps
	win::IWin* pWin_ = nullptr;
	uiohook::IUiohook* pUiohook_ = nullptr;
	inspect::IAutomation* pAutomation_ = nullptr;
private:
	std::atomic_bool ready_ = false;
	std::atomic_bool open_ = false;
	const HMODULE module_;
	std::string path_python_;
	std::string path_module_;
	std::string path_data_;
	std::vector<std::thread> threads_;
	std::thread automation_thread_;
	std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};


extern Core* __gpCore;
extern Core* CoreGet();
extern HINSTANCE __gpHinstance;

#endif //CAPTURE_CAPTURE_H
