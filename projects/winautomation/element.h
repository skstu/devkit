#ifndef __PROJECTS_WINAUTOMATION_ELEMENT_H_
#define __PROJECTS_WINAUTOMATION_ELEMENT_H_

class Point final : public IAutomation::IPoint {
public:
  Point();
  ~Point();
  void Release() const override final;
};
#endif //__PROJECTS_WINAUTOMATION_ELEMENT_H_