#ifndef __PROJECTS_INSPECTION_OVERLAY_OVERLAY_H_
#define __PROJECTS_INSPECTION_OVERLAY_OVERLAY_H_

class Overlay final : public IOverlay {
public:
  Overlay();
  virtual ~Overlay();

private:
  void Init();
  void UnInit();

protected:
  bool Start(const OverlayWindowType &) override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;
  void OnUioEvent(const uiohook::IUioEvent *) const override final;
  bool Screenshot(const OverlayWindowType &) const override final;

  void SetPosition(const long &x, const long &y, const long &cx,
                   const long &cy) const override final;

private:
  std::atomic_bool open_ = false;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  std::vector<IWindow *> window_s_;
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

#endif ///__PROJECTS_INSPECTION_OVERLAY_OVERLAY_H_