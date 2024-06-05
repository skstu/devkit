#ifndef __WINAUTOMATION_H_
#define __WINAUTOMATION_H_

#include "interface.h"

namespace win {

class IAutomation : public Interface<IAutomation> {
public:
  class IPoint {
  public:
    virtual void Release() const = 0;
  };
  class IRect {
  public:
    virtual void Release() const = 0;
  };
  class IPosition {
  public:
    virtual void Release() const = 0;
  };
  class IElement {
  public:
    virtual const IPoint *GetCaprutePoint() const = 0;
    virtual const IRect *GetRect() const = 0;
    virtual const IPosition *GetPos() const = 0;
    virtual void Release() const = 0;
  };
  using tfCapruteFinishCb = std::function<void(const IElement *)>;

public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual void RegisterCapruteFinishCb(const tfCapruteFinishCb &) = 0;
};
} // namespace win

#endif //__WINAUTOMATION_H_