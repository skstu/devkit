#ifndef __CVPP_H_
#define __CVPP_H_
#include "interface.h"
namespace cvpp {
	class ICvpp : public Interface<ICvpp> {
	public:
		virtual void Stop() = 0;
		virtual bool Start() = 0;
		virtual bool Ready() const = 0;
		virtual void Release() const = 0;
	};
} // namespace cvpp

#endif //__CVPP_H_