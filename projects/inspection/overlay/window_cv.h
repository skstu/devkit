#ifndef __PROJECTS_WINOVERLAY_WINDOWCV_H_
#define __PROJECTS_WINOVERLAY_WINDOWCV_H_

class WindowCv final : public IWindow {
public:
  WindowCv();
  virtual ~WindowCv();

private:
  void Init();
  void UnInit();

protected:
  bool Screenshot() const override final;
  void Release() const override final;
  void OnWindowIdle() override final;
  void OnUioEvent(const uiohook::IUioEvent *) const override final;
};
#endif //__PROJECTS_WINOVERLAY_WINDOWCV_H_