#ifndef CAPTURE_CAPTURE_H
#define CAPTURE_CAPTURE_H

class Core {
public:
 Core(const HMODULE&);
 ~Core();
public:
 bool Ready() const;
private:
 void Init();
 void UnInit();
public:
	bool Start();
	void Stop();
private:
 std::atomic_bool ready_ = false;
 std::atomic_bool open_ = false;
 const HMODULE module_;
 std::string path_python_;
 std::string path_;
 inspect::Inspection* pInspection_ = nullptr;
 uiohook::IUiohook* pUiohook_ = nullptr;
};


extern Core* __gpCore;
extern Core* CoreGet();
extern HINSTANCE __gpHinstance;

#endif //CAPTURE_CAPTURE_H
