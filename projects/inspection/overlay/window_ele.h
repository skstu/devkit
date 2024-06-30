#ifndef __PROJECTS_WINOVERLAY_WINDOWELE_H_
#define __PROJECTS_WINOVERLAY_WINDOWELE_H_

class WindowEle final : public IWindow {
public:
  enum class LayeredWindowType : unsigned int {
    Rectangle = 0,
    Circular = 1,
  };

public:
  WindowEle();
  virtual ~WindowEle();

private:
  void Init();
  void UnInit();
  void SetRegion(const HWND &) const;
  void OnPaint(const HWND &) const;
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);

protected:
  bool Screenshot() const override final;
  void Release() const override final;
  void OnWindowIdle() override final;
  void OnCreateWindowSuccess() const override final;
  void SetPosition(const long &x, const long &y, const long &cx,
                   const long &cy) const override final;
  void OnUioEvent(const uiohook::IUioEvent *) const override final;

private:
  LayeredWindowType layered_window_type_;
};
#endif //__PROJECTS_WINOVERLAY_WINDOWELE_H_