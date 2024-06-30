#ifndef __PROJECTS_INSPECTION_AUTOMATION_CONFIG_H_
#define __PROJECTS_INSPECTION_AUTOMATION_CONFIG_H_
class Config final : public IAutomation::IConfig {
public:
  Config();
  virtual ~Config();

public:
  void EnableFocusChangedEvent(const bool &) override final;
  void EnableMouseClickedEvent(const bool &) override final;
  void EnableMouseMoveEvent(const bool &) override final;
  void EnableCapture(const bool &) override final;
  void EnableFocusChangedEventHandler(const bool &) override final;

public:
  bool EnableFocusChangedEvent() const override final;
  bool EnableMouseClickedEvent() const override final;
  bool EnableMouseMoveEvent() const override final;
  bool EnableCaptureIs() const override final;
  bool EnableFocusChangedEventHandler() const override final;

private:
  std::atomic_bool enable_focus_changed_event_;
  std::atomic_bool enable_mouse_clicked_event_;
  std::atomic_bool enable_mouse_move_event_;
  std::atomic_bool enable_focus_changed_event_handler_;
  std::atomic_bool enable_capture_;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

#endif //__PROJECTS_INSPECTION_AUTOMATION_CONFIG_H_