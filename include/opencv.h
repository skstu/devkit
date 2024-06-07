#ifndef __OPENCV_H_
#define __OPENCV_H_

#include "interface.h"

namespace opencv {
class IOpencv : public Interface<IOpencv> {
public:
  virtual void Stop() = 0;
  virtual bool Start() = 0;
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
};
} // namespace opencv

#endif //__OPENCV_H_