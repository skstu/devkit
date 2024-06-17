#ifndef __PROJECTS_INSPECTION_INSPECT_H_
#define __PROJECTS_INSPECTION_INSPECT_H_

class Inspect final : public Inspection {
public:
  Inspect();
  virtual ~Inspect();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;
  Inspection::IConfig *ConfigGet() const override final;

private:
  std::atomic_bool ready_ = false;
  std::atomic_bool open_ = false;
  IUiohook *pUiohook_ = nullptr;
  IAutomation *pAutomation_ = nullptr;
  IOverlay *pOverlay_ = nullptr;
  ICvpp *pCvpp_ = nullptr;
  Config *config_ = nullptr;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Inspect *__gpInspect;

#endif ///__PROJECTS_INSPECTION_INSPECT_H_