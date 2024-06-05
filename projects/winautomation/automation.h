#ifndef __PROJECTS_WINAUTOMATION_AUTOMATION_H_
#define __PROJECTS_WINAUTOMATION_AUTOMATION_H_

class Automation final : public IAutomation {
public:
  Automation();
  virtual ~Automation();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;
  void RegisterCapruteFinishCb(const tfCapruteFinishCb &) override final;

private:
  void OnCapruteFinish(const IAutomation::IElement *) const;

private:
  std::vector<std::thread> threads_;
  std::atomic_bool open_ = false;
  std::atomic_bool ready_ = false;
  std::atomic_bool coinit_ = false;
  void Process();
  tfCapruteFinishCb caprute_finish_cb_ = nullptr;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Automation *__gpAutomation;

#endif ///__PROJECTS_WINAUTOMATION_AUTOMATION_H_