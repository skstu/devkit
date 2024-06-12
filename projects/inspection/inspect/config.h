#ifndef __PROJECTS_INSPECTION_CONFIG_H_
#define __PROJECTS_INSPECTION_CONFIG_H_

class Config final : public Inspection::IConfig {
public:
  Config();
  void Release() const;

private:
  virtual ~Config();

protected:
};
#endif //__PROJECTS_INSPECTION_CONFIG_H_