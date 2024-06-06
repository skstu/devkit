#ifndef __LIBUIOHOOK_H_
#define __LIBUIOHOOK_H_

#include "interface.h"

namespace uiohook {

class IUiohook : public Interface<IUiohook> {
public:
  using tfMouseMoveCb = std::function<void(const long &, const long &)>;
  using tfCaptureFinishCb =
      std::function<void(const long &, const long &, bool &)>;

public:
  virtual void Release() const = 0;
  virtual bool Ready() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual void RegisterMouseMoveCb(const tfMouseMoveCb &) = 0;
  virtual void RegisterCaptureFinishCb(const tfCaptureFinishCb &) = 0;
};
} // namespace uiohook

#endif //__LIBUIOHOOK_H_