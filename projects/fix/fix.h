#ifndef __PROJECTS_FIX_FIX_H_
#define __PROJECTS_FIX_FIX_H_

class Fix final : public IFix {
public:
  Fix();
  virtual ~Fix();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;

private:
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
extern Fix *__gpFix;

#endif ///__PROJECTS_FIX_FIX_H_