#ifndef __PROJECTS_WINOVERLAY_OVERLAY_H_
#define __PROJECTS_WINOVERLAY_OVERLAY_H_

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
  void AppendPos(const long &, const long &, const long &,
                 const long &) override final;
  bool Screenshot(const tfStreamCb &) const override final;

private:
  void WindowThread();
  HWND hwnd_ = nullptr;
  std::vector<std::thread> threads_;
  std::atomic_bool open_ = false;
  stl::container::queue<RECT> posq_;
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