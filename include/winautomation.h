#ifndef __WINAUTOMATION_H_
#define __WINAUTOMATION_H_

#include "interface.h"

namespace win {
using PixelType = double;

class IPoint {
public:
  virtual void Release() const = 0;
  virtual const PixelType &GetX() const = 0;
  virtual const PixelType &GetY() const = 0;
};
class IRect {
public:
  virtual void Release() const = 0;
  virtual const PixelType &Left() const = 0;
  virtual const PixelType &Top() const = 0;
  virtual const PixelType &Right() const = 0;
  virtual const PixelType &Bottom() const = 0;
  virtual PixelType Width() const = 0;
  virtual PixelType Height() const = 0;
};
class IPosition {
public:
  virtual const IPoint *GetPoint() const = 0;
  virtual const IRect *GetRect() const = 0;
  virtual void Release() const = 0;
};
class IElement {
public:
  virtual const IPoint *GetCaprutePoint() const = 0;
  virtual const IPosition *GetPosition() const = 0;
  virtual void Release() const = 0;
};

class IAutomation : public Interface<IAutomation> {
protected:
  using tfElementCaptureFinishCb = std::function<void(const IElement *)>;

public:
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual IElement *GetElementOnUnderMouse(const long &,
                                           const long &) const = 0;
  virtual void
  RegisterElementCaptureFinishCb(const tfElementCaptureFinishCb &) = 0;
};
} // namespace win

#endif //__WINAUTOMATION_H_