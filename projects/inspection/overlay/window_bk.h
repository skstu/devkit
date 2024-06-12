#ifndef __PROJECTS_WINOVERLAY_WINDOWBK_H_
#define __PROJECTS_WINOVERLAY_WINDOWBK_H_

class WindowBk final : public IWindow {
public:
  WindowBk();
  virtual ~WindowBk();

protected:
  bool Screenshot() const override final;
  void Release() const override final;
  void OnCreateWindowSuccess() const override final;
  void OnUioEvent(const uiohook::IUioEvent *) const override final;
};
#endif //__PROJECTS_WINOVERLAY_WINDOWBK_H_