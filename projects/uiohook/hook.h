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
  void RegisterMouseMoveCb(const tfMouseMoveCb &) override final;
  void RegisterCaptureFinishCb(const tfCaptureFinishCb &) override final;
  IUioevent *CreateEvent() override final;

public:
  void OnMouseMove(const long &, const long &) const;
  void OnCaptureFinish(const long &, const long &, bool &) const;
  bool GetControlState() const;
  void SetControlState(const bool &);
  void AppendEvent(const uiohook_event *);

private:
  tfMouseMoveCb mouse_move_cb_ = nullptr;
  tfCaptureFinishCb capture_finish_cb_ = nullptr;
  std::vector<std::thread> threads_;
  void Perform();
  void EventNotify();
  std::atomic_bool open_ = false;
  std::atomic_bool processed_control_ = false;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  stl::container::queue<uiohook_event> uiohook_event_q_;
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