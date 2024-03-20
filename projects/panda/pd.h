#ifndef __PROJECTS_PANDA_PD_H_
#define __PROJECTS_PANDA_PD_H_

class Panda final : public IPanda {
public:
  Panda();
  virtual ~Panda();

protected:
  bool Ready() const override final;
  void Release() const override final;
  IBrowserFingerprint *CreateBF() const override final;
  IBrowserExtensions *CreateEXT() const override final;
  void Test() const override final;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Panda *__gpPanda;

#endif ///__PROJECTS_PANDA_PD_H_