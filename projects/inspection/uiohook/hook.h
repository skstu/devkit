#ifndef __PROJECTS_UIOHOOK_HOOK_H_
#define __PROJECTS_UIOHOOK_HOOK_H_

class Hook final : public IUiohook {
public:
  Hook();
  virtual ~Hook();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;
  void MouseSetTo(const long &x, const long &y) const override final;
  void MouseMoveTo(long x, long y, long step) const override final;
  void MouseClick(const UioVirtualMouseButtons &, const long &x, const long &y,
                  const unsigned long &count) const override final;
  void MouseWheel(const long &x, const long &y, const long &amount,
                  const long &rotation) const override final;
  void MouseDragged(const long &to_x, const long &to_y, const long &from_x,
                    const long &from_y) const override final;
  void UnregisterEvent(const unsigned long long &) override final;
  void RegisterEventCb(const unsigned long long &,
                       const tfEventCb &) override final;
  void Free(void **) const override final;
  bool SetClipboardText(const char *, const long &) const override final;
  bool GetClipboardText(char **, long *) const override final;

private:
  void __MouseSetPos(const long &x, const long &y,
                     const UioVirtualMouseButtons &btnType =
                         UioVirtualMouseButtons::UIO_MOUSE_BUTTON_ANY) const;
  void __MouseMoveTo(long x, long y, long step,
                     const UioVirtualMouseButtons &btnType =
                         UioVirtualMouseButtons::UIO_MOUSE_BUTTON_ANY) const;

public:
  void SetKeyState(const UioVirtualKeyCodes &, const bool &);
  void SetMouseButtonState(const UioVirtualMouseButtons &, const bool &);
  void AppendEvent(const uiohook_event *);

private:
  std::vector<std::thread> threads_;
  void Perform();
  void Worker();
  void OnEvent(const uiohook_event &) const;
  std::atomic_bool open_ = false;
  std::atomic_bool processed_control_ = false;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  stl::container::queue<uiohook_event> uiohook_event_q_;
  std::map<unsigned long long, tfEventCb> uioevent_cbs_;
  std::map<UioVirtualKeyCodes, bool> uio_virtual_key_pressed_s_;
  std::map<UioVirtualMouseButtons, bool> uio_virtual_mouse_pressed_s_;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Hook *__gpHook;

#endif ///__PROJECTS_UIOHOOK_HOOK_H_