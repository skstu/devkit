#ifndef __PROJECTS_WINOVERLAY_WINDOWUI_H_
#define __PROJECTS_WINOVERLAY_WINDOWUI_H_

class WindowUi final : public IWindow {
public:
  WindowUi();
  virtual ~WindowUi();

protected:
  bool Screenshot() const override final;
  void Release() const override final;
  void OnCreateWindowSuccess() const override final;
  void SetPosition(const long &x, const long &y, const long &cx,
                   const long &cy) const override final;
};

#endif //__PROJECTS_WINOVERLAY_WINDOWUI_H_