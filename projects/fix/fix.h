#ifndef __PROJECTS_FIX_FIX_H_
#define __PROJECTS_FIX_FIX_H_

class Fix final : public IFix {
public:
  Fix();
  virtual ~Fix();

protected:
  bool Ready() const override final;
  void Release() const override final;
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