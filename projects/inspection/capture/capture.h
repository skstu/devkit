#ifndef __PROJECTS_INSPECTION_CAPTURE_H_
#define __PROJECTS_INSPECTION_CAPTURE_H_

class Capture final : public ICapture {
public:
  Capture();
  virtual ~Capture();

private:
  void Init();
  void UnInit();

protected:
  bool Ready() const override final;
  void Release() const override final;
  bool Start() override final;
  void Stop() override final;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Capture *__gpCapture;

#endif ///__PROJECTS_INSPECTION_CAPTURE_H_