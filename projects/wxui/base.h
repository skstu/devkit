#ifndef __PROJECTS_WXUI_BASE_H_
#define __PROJECTS_WXUI_BASE_H_

class UIBase {
public:
  UIBase();
  virtual ~UIBase();

private:
  void Init();
  void UnInit();

protected:
  virtual void MainThread();

private:
  std::vector<std::thread> threads_;
};
#endif ///__PROJECTS_WXUI_BASE_H_
