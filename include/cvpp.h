#ifndef __CVPP_H_
#define __CVPP_H_
#include "interface.h"
namespace cvpp {
	class ICvpp : public Interface<ICvpp> {
	public:
		class IStream {
		public:
			virtual void SetData(const void*, const size_t&) = 0;
			virtual void* GetBuffer() const = 0;
			virtual size_t GetSize() const = 0;
			virtual void Release() const = 0;
			virtual bool Empty() const = 0;
		};

		class IRange {
		public:
			virtual long X() const = 0;
			virtual long Y() const = 0;
			virtual long CX() const = 0;
			virtual long CY() const = 0;
			virtual void Release() const = 0;
		};
	public:
		virtual void Stop() = 0;
		virtual bool Start() = 0;
		virtual bool Ready() const = 0;
		virtual void Release() const = 0;
		virtual IStream* CreateStream() const = 0;
		virtual IRange* Find(const IStream* src, const IStream* dst, double threshold_value) const = 0;
		virtual void TestCv(const char*, size_t, const char*, size_t) const = 0;
	};
} // namespace cvpp

#endif //__CVPP_H_