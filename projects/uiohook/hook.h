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

private:
  std::vector<std::thread> threads_;
  void Perform();
  std::atomic_bool open_ = false;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Hook *__gphook;

#endif ///__PROJECTS_UIOHOOK_HOOK_H_