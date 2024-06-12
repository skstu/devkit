#ifndef CVPP_OVERLAY_H
#define CVPP_OVERLAY_H

class Overlay final {
public:
 Overlay();
 virtual ~Overlay();

private:
 void Init();
 void UnInit();

public:
 bool Start()  ;
 void Stop()  ;
 bool Ready() const  ;
 void Release() const  ;
 bool Screenshot(const std::function<void(const char*,const size_t&)>&) const  ;
private:
 void WindowThread();
 const std::wstring class_name_ = LR"(OverlayWindowClassFromCvpp)";
 HWND hwnd_ = nullptr;
 std::vector<std::thread> threads_;
 std::atomic_bool open_ = false;
 IUiohook* uiohook_ = nullptr;
 std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
 ULONG_PTR gdiplustoken_ = 0;
 Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = { 0 };
 static void OnMouseMove(const long& x, const long& y,void*);
#if 0
 stl::container::queue<IElement*> element_s_;
 stl::container::queue<std::tuple<const long, const long>> mouse_move_point_s_;
 IElement* element_last_ = nullptr;
#endif
};

#endif //CVPP_OVERLAY_H
