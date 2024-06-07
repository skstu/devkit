#ifndef __PROJECTS_UIOHOOK_NOTIFY_H_
#define __PROJECTS_UIOHOOK_NOTIFY_H_

class Event final : public IUioevent {
public:
  Event();
  virtual ~Event();

protected:
  void Release() const override final;

private:
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

#endif //__PROJECTS_UIOHOOK_NOTIFY_H_
