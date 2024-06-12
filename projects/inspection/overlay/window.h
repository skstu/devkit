#ifndef __PROJECTS_WINOVERLAY_WINDOW_H_
#define __PROJECTS_WINOVERLAY_WINDOW_H_

class IWindow {
public:
  IWindow(const OverlayWindowType &, const std::wstring &,
          const unsigned long &, const unsigned long &, const WNDPROC &);
  ~IWindow();

private:
  void Init();
  void UnInit();

public:
  virtual void Release() const = 0;
  virtual bool Create();
  virtual void Destroy();
  virtual void Show(const bool &) const;
  virtual void OnCreateWindowSuccess() const;
  virtual const OverlayWindowType &Type() const;

private:
  void OnDpiInit();

public:
  virtual void OnUioEvent(const uiohook::IUioEvent *) const;
  virtual bool Screenshot() const;
  virtual void SetPosition(const long &x, const long &y, const long &cx,
                           const long &cy) const;

protected:
  virtual void WindowThread();
  virtual void OnWindowIdle();
  std::vector<std::thread> threads_;
  HWND hwnd_ = nullptr;
  HINSTANCE handle_ = nullptr;
  std::atomic_bool open_ = false;
  const OverlayWindowType type_;
  const std::wstring class_name_;
  const unsigned long exstyle_;
  const unsigned long style_;
  const WNDPROC wndproc_;
  SIZE default_window_size_ = {0};
  POINT default_window_point_ = {0};
  float dpiScale = 1.0f;
  RECT desktop_rect = {0};
};
#endif //__PROJECTS_WINOVERLAY_WINDOW_H_