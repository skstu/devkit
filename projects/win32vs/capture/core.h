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
private:
 std::atomic_bool ready_ = false;
 inspect::Inspection* pInspection_ = nullptr;
 const HMODULE module_;
 std::string path_python_;
 std::string path_;
};


extern Core* __gpCore;
extern Core* CoreGet();

#endif //CAPTURE_CAPTURE_H
