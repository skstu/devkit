#ifndef __PROJECTS_PANDA_BEXT_H_
#define __PROJECTS_PANDA_BEXT_H_

class BrowserExtensions final : public IBrowserExtensions {
public:
  BrowserExtensions();
  ~BrowserExtensions();

protected:
  void Release() const override final;
};

#endif ///__PROJECTS_PANDA_BEXT_H_