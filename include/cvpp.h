#ifndef __CVPP_H_
#define __CVPP_H_

namespace cvpp {
	class ICvpp {
	public:
		virtual void Stop() = 0;
		virtual bool Start() = 0;
		virtual bool Ready() const = 0;
		virtual void Release() const = 0;
	};
} // namespace cvpp

#endif //__CVPP_H_