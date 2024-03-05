#ifndef __WXUI_H_
#define __WXUI_H_

#include "interface.h"

namespace wxui {
class IWxui : public Interface<IWxui> {
public:
  virtual bool Ready() const = 0;
  virtual void Release() const = 0;
  virtual bool UICreate() = 0;
  virtual void UIDestroy() = 0;
};
} // namespace wxui

#endif ///__WXUI_H_
