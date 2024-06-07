#ifndef __PROJECTS_OPENCV_CV_H_
#define __PROJECTS_OPENCV_CV_H_

class Opencv final : public IOpencv {
public:
  Opencv();
  virtual ~Opencv();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;

private:
  void Perform();
  std::vector<std::thread> threads_;
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
extern Opencv *__gpOpencv;

#endif ///__PROJECTS_OPENCV_CV_H_