#ifndef __LIBUIOHOOK_H_
#define __LIBUIOHOOK_H_

#include "interface.h"

namespace uiohook {

enum class UioeventType {
  UIO_EVENT_UNKNOWN = 0,
  UIO_EVENT_MOUSE = 1,
  UIO_EVENT_KEYBOARD = 2,
  UIO_EVENT_INPUT_MOUSE = 3,
  UIO_EVENT_INPUT_KEYBOARD = 4,
};

class IUioevent {
public:
  virtual void Release() const = 0;
};

class IUiohook : public Interface<IUiohook> {
public:
  using tfMouseMoveCb = std::function<void(const long &, const long &)>;
  using tfCaptureFinishCb =
      std::function<void(const long &, const long &, bool &)>;

public:
  virtual IUioevent *CreateEvent() = 0;
  virtual void Release() const = 0;
  virtual bool Ready() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual void RegisterMouseMoveCb(const tfMouseMoveCb &) = 0;
  virtual void RegisterCaptureFinishCb(const tfCaptureFinishCb &) = 0;
};
} // namespace uiohook

#endif //__LIBUIOHOOK_H_