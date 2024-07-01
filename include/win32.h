#ifndef __WIN_H_
#define __WIN_H_
#include "interface.h"
namespace win {
	class IWindowInfo {
	public:
		virtual const char* GetKey() const = 0;
		virtual void SetKey(const char*) = 0;
		virtual void* Hwnd() const = 0;
		virtual const wchar_t* WindowText() const = 0;
		virtual const wchar_t* WindowClass() const = 0;
		virtual const wchar_t* ImagePathname() const = 0;
		virtual void Release() const = 0;
		virtual void* Root() const = 0;
		virtual void* Parent() const = 0;
		virtual unsigned long ProcessId() const = 0;
		virtual bool fromJson(const char*) = 0;
		virtual const char* toJson() = 0;
	};

	class IWindowInfoArray {
	public:
		virtual IWindowInfo* Begin() const = 0;
		virtual IWindowInfo* End() const = 0;
		virtual IWindowInfo* Next(const size_t&) const = 0;
		virtual size_t Total() const = 0;
		virtual void Release() const = 0;
		virtual const char* toJson() = 0;
		virtual bool fromJson(const char*) = 0;
	};

	class IDirectoryElement {
	public:
		virtual void Release() const = 0;
		virtual bool IsDir() const = 0;
		virtual const char* Path() const = 0;
		virtual const char* Name() const = 0;
		virtual long long CreateTime() const = 0;
		virtual long long AccessTime() const = 0;
		virtual long long WriteTime() const = 0;
 	};
	class IDirectoryArray {
	public:
		virtual IDirectoryElement* Next(const size_t&) const = 0;
		virtual size_t Total() const = 0;
		virtual void Release() const = 0;
	};
	class IFileArray {
	public:
	};
	class IWin : public Interface<IWin> {
	public:
		virtual IWindowInfo* CreateWindowInfo(const char* json) const = 0;
		virtual void Release() const = 0;
		virtual void FreePtr(void**) const = 0;
		virtual void DeletePtr(void**) const = 0;
		virtual void GeneratePathKey(const char*, const size_t&, char**, size_t*) const = 0;
		virtual void GeneratePathKey(const wchar_t*, const size_t&, wchar_t**, size_t*) const = 0;
		//!@ Hwnd
		virtual void EnumWindows(IWindowInfoArray**) const = 0;
		virtual void EnumChildWindows(const void* hProcessWnd, IWindowInfoArray**) const = 0;
		virtual void EnumProcessWindows(const unsigned long& ProcessId, IWindowInfoArray**) const = 0;
		//!@ Conv
		virtual bool WStringToUTF8(const wchar_t*, const size_t&, char**, size_t*) const = 0;
		virtual bool WStringToMBytes(const wchar_t*, const size_t&, char**, size_t*) const = 0;
		virtual bool MBytesToWString(const char*, const size_t&, wchar_t**, size_t*) const = 0;
		virtual bool UTF8ToWString(const char*, const size_t&, wchar_t**, size_t*) const = 0;
		virtual bool UTF8ToMBytes(const char*, const size_t&, char**, size_t*) const = 0;
		virtual bool MBytesToUTF8(const char*, const size_t&, char**, size_t*) const = 0;
		//!@ Process
		virtual bool ProcessCreate(
			const char* exePathname,
			const char* startParamenters,
			unsigned long* pid,
			const bool& Inheriting_parent_process /* = false*/,
			const bool& isShow /*= false*/,
			const unsigned long& wait_time /*= 10000*/) const = 0;
		//!@ Encryption
		virtual void MD5(const char*, const size_t&, char**, size_t*) const = 0;
		virtual void WemadeEncode(const char*, const size_t&, char**, size_t*) const = 0;
		virtual void WemadeDecode(const char*, const size_t&, char**, size_t*) const = 0;
		virtual void Base64Encode(const char*, const size_t&, char**, size_t*) const = 0;
		virtual void Base64Decode(const char*, const size_t&, char**, size_t*) const = 0;
		//!@ Folder
		virtual bool CreateFolder(const char* path) const = 0;
		virtual IDirectoryArray* EnumFolder(const char* path, const bool& recursion) const = 0;
	};

} // namespace win

#endif //__WIN_H_