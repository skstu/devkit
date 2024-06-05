#ifndef __PROJECTS_WINOVERLAY_OVERLAY_H_
#define __PROJECTS_WINOVERLAY_OVERLAY_H_
#if 0

class Overlay {
public:
  Overlay();
  ~Overlay();

private:
  void Init();
  void UnInit();

public:
  bool Create();
  void Destory();
  void Release() const;
  void Update(const RECT &) const;

private:
  HWND hwnd_ = nullptr;
  IUIAutomation *pAutomation_ = nullptr;
  std::atomic_bool coinit_ = false;
  std::atomic_bool open_ = false;
  std::atomic_bool ready_ = false;
  std::vector<std::thread> threads_;
  void WindowProc();
  ULONG_PTR gdiplustoken_ = 0;
  Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = {0};
};

#endif
class Overlay final : public IOverlay {
public:
  Overlay();
  virtual ~Overlay();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;

private:
  void WindowThread();
  HWND hwnd_ = nullptr;
  std::vector<std::thread> threads_;
  std::atomic_bool open_ = false;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Overlay *__gpOverlay;

#endif ///__PROJECTS_WINOVERLAY_OVERLAY_H_