#if !defined(__WIN32_HPP__)
#define __WIN32_HPP__


class Win32 final : public IWin {
public:
	Win32();
	void Release() const override final;
private:
	virtual ~Win32();
protected:
	IWindowInfo* CreateWindowInfo(const char* json) const override final;
	void FreePtr(void**) const override final;
	void DeletePtr(void**) const override final;
	void EnumWindows(IWindowInfoArray**) const override final;
	void GeneratePathKey(const char*, const size_t&, char**, size_t*) const override final;
	void GeneratePathKey(const wchar_t*, const size_t&, wchar_t**, size_t*) const override final;
	void EnumChildWindows(const void* hProcessWnd, IWindowInfoArray**) const override final;
	void EnumProcessWindows(const unsigned long& ProcessId, IWindowInfoArray**) const override final;
	bool WStringToUTF8(const wchar_t*, const size_t&, char**, size_t*) const override final;
	bool WStringToMBytes(const wchar_t*, const size_t&, char**, size_t*) const override final;
	bool MBytesToWString(const char*, const size_t&, wchar_t**, size_t*) const override final;
	bool UTF8ToWString(const char*, const size_t&, wchar_t**, size_t*) const override final;
	bool UTF8ToMBytes(const char*, const size_t&, char**, size_t*) const override final;
	bool MBytesToUTF8(const char*, const size_t&, char**, size_t*) const override final;
	bool ProcessCreate(
		const char* exePathname,
		const char* startParamenters,
		unsigned long* pid,
		const bool& Inheriting_parent_process /* = false*/,
		const bool& isShow /*= false*/,
		const unsigned long& wait_time /*= 10000*/) const override final;

	void MD5(const char*, const size_t&, char**, size_t*) const override final;
	void WemadeEncode(const char*, const size_t&, char**, size_t*) const override final;
	void WemadeDecode(const char*, const size_t&, char**, size_t*) const override final;
	void Base64Encode(const char*, const size_t&, char**, size_t*) const override final;
	void Base64Decode(const char*, const size_t&, char**, size_t*) const override final;

	IDirectoryArray* EnumFolder(const char* path, const bool& recursion) const override final;
	bool CreateFolder(const char* path) const override final;
};

#ifdef __cplusplus
extern "C" {
#endif
	SHARED_API void* interface_init(void*, unsigned long);
	SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Win32* __gpWin32;


#endif //__WIN32_HPP__
