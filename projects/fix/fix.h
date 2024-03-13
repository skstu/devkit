#ifndef __PROJECTS_FIX_FIX_H_
#define __PROJECTS_FIX_FIX_H_

class Fix final : public IFix {
public:
  Fix();
  virtual ~Fix();

protected:
  bool Start(const char *settings) override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;

private:
  std::atomic_bool open_ = false;
  std::string settings_;
  FIX::Acceptor *acceptor_ = nullptr;
  FIX::Initiator *initiator_ = nullptr;
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