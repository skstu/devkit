#include "stdafx.h"
#include <rapidjson.h>
using namespace shared;

namespace {
	class __WindowInfo final : public IWindowInfo {
		friend class __WindowInfoArray;
	public:
		__WindowInfo(const Win::WindowInfo& info) : info_(info) {

		}
		__WindowInfo(const std::string& json) {
			fromJson(json.c_str());
		}
		virtual ~__WindowInfo() {

		}
		void Release() const override final {
			delete this;
		}
	protected:
		const char* toJson() override final {
			json_.clear();

			rapidjson::Document doc(rapidjson::Type::kObjectType);
			doc.AddMember(rapidjson::Value().SetString("key", doc.GetAllocator()),
				rapidjson::Value().SetString(info_.Key[0] ? info_.Key : "", doc.GetAllocator()), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("path", doc.GetAllocator()),
				rapidjson::Value().SetString(IConv::WStringToUTF8(info_.ImagePathname).c_str(), doc.GetAllocator()), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("text", doc.GetAllocator()),
				rapidjson::Value().SetString(IConv::WStringToUTF8(info_.Text).c_str(), doc.GetAllocator()), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("class", doc.GetAllocator()),
				rapidjson::Value().SetString(IConv::WStringToUTF8(info_.Class).c_str(), doc.GetAllocator()), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("pid", doc.GetAllocator()),
				rapidjson::Value().SetUint(info_.ProcessId), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("root", doc.GetAllocator()),
				rapidjson::Value().SetUint64(reinterpret_cast<uint64_t>(info_.Root)), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("parent", doc.GetAllocator()),
				rapidjson::Value().SetUint64(reinterpret_cast<uint64_t>(info_.Parent)), doc.GetAllocator());
			doc.AddMember(rapidjson::Value().SetString("handle", doc.GetAllocator()),
				rapidjson::Value().SetUint64(reinterpret_cast<uint64_t>(info_.hWnd)), doc.GetAllocator());

			json_ = Json::toString(doc);
			return json_.c_str();
		}
		bool fromJson(const char* json) override final {
			bool result = false;
			memset(&info_, 0x00, sizeof(info_));
			do {
				if (!json)
					break;
				rapidjson::Document obj;
				if (obj.Parse(json).HasParseError())
					break;
				if (!obj.IsObject())
					break;
				if (obj.HasMember("key") && obj["key"].IsString()) {
					SetKey(obj["key"].GetString());
				}
				if (obj.HasMember("path") && obj["path"].IsString()) {
					std::wstring wstrPath = IConv::UTF8ToWString(obj["path"].GetString());
					wcscpy(info_.ImagePathname, wstrPath.c_str());
				}
				if (obj.HasMember("text") && obj["text"].IsString()) {
					std::wstring wstrText = IConv::UTF8ToWString(obj["text"].GetString());
					wcscpy(info_.Text, wstrText.c_str());
				}
				if (obj.HasMember("class") && obj["class"].IsString()) {
					std::wstring wstrClass = IConv::UTF8ToWString(obj["class"].GetString());
					wcscpy(info_.Class, wstrClass.c_str());
				}
				if (obj.HasMember("pid") && obj["pid"].IsUint())
					info_.ProcessId = obj["pid"].GetUint();
				if (obj.HasMember("root") && obj["root"].IsNumber())
					info_.Root = reinterpret_cast<HWND>(obj["root"].GetUint64());
				if (obj.HasMember("parent") && obj["parent"].IsNumber())
					info_.Parent = reinterpret_cast<HWND>(obj["parent"].GetUint64());
				if (obj.HasMember("handle") && obj["handle"].IsNumber())
					info_.hWnd = reinterpret_cast<HWND>(obj["handle"].GetUint64());
			} while (0);
			return result;
		}
		const char* GetKey() const override final {
			return info_.Key;
		}
		void SetKey(const char* key) override final {
			memset(info_.Key, 0x00, sizeof(info_.Key));
			size_t key_size = strlen(key);
			if (key_size > 0)
				memcpy(info_.Key, key, key_size);
		}
		void* Hwnd() const override final {
			return reinterpret_cast<void*>(info_.hWnd);
		}
		void* Root() const override final {
			return reinterpret_cast<void*>(info_.Root);
		}
		void* Parent() const {
			return reinterpret_cast<void*>(info_.Parent);
		}
		unsigned long ProcessId() const override final {
			return info_.ProcessId;
		}
		const wchar_t* WindowText() const  override final {
			return info_.Text;
		}
		const wchar_t* WindowClass() const  override final {
			return info_.Class;
		}
		const wchar_t* ImagePathname() const  override final {
			return info_.ImagePathname;
		}
	private:
		std::string json_;
		Win::WindowInfo info_;
	};

	class __WindowInfoArray final : public IWindowInfoArray {
	public:
		__WindowInfoArray(const std::vector<Win::WindowInfo>& infos) {
			for (auto it = infos.begin(); it != infos.end(); ++it) {
				infos_.emplace_back(new __WindowInfo(*it));
			}
		}
		virtual ~__WindowInfoArray() {
			for (auto it = infos_.begin(); it != infos_.end(); ++it) {
				(*it)->Release();
			}
			infos_.clear();
		}
	protected:
		void Release() const override final {
			delete this;
		}
		const char* toJson() override final {
			json_.clear();
			for (const auto& info : infos_) {
				json_.append(info->toJson()).append(",");
			}
			if (!json_.empty())
				json_.pop_back();
			json_.insert(0, "[");
			json_.append("]");
			return json_.c_str();
		}
		bool fromJson(const char* json) override final {
			bool result = false;
			for (auto it = infos_.begin(); it != infos_.end(); ++it) {
				(*it)->Release();
			}
			infos_.clear();
			do {
				if (!json)
					break;
				rapidjson::Document doc;
				if (doc.Parse(json).HasParseError())
					break;
				if (!doc.IsArray())
					break;
				for (auto it = doc.Begin(); it != doc.End(); ++it) {
					if (!it->IsObject())
						continue;
					std::string jsonStr = Json::toString(*it);
					infos_.emplace_back(new __WindowInfo(jsonStr));
				}

				result = true;
			} while (0);
			return result;
		}
		IWindowInfo* Begin() const override final {
			IWindowInfo* result = nullptr;
			do {
				if (infos_.empty())
					break;
				result = dynamic_cast<IWindowInfo*>(*infos_.begin());
			} while (0);
			return result;
		}
		IWindowInfo* End() const override final {
			IWindowInfo* result = nullptr;
			do {
				if (infos_.empty())
					break;
				result = dynamic_cast<IWindowInfo*>(*std::prev(infos_.end()));
			} while (0);
			return result;
		}
		IWindowInfo* Next(const size_t& index) const override final {
			IWindowInfo* result = nullptr;
			do {
				const size_t total = infos_.size();
				if (index >= total)
					break;
				result = dynamic_cast<IWindowInfo*>(infos_[index]);
			} while (0);
			return result;
		}
		size_t Total() const override final {
			return infos_.size();
		}
	private:
		std::vector<__WindowInfo*> infos_;
		std::string json_;
	};
}

void Win32::MD5(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::Encryption::MD5(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);

}
IWindowInfo* Win32::CreateWindowInfo(const char* json) const {
	IWindowInfo* result = nullptr;
	do {
		if (!json)
			break;
		result = dynamic_cast<IWindowInfo*>(new __WindowInfo(json));
	} while (0);
	return result;
}

void Win32::WemadeEncode(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::Encryption::WemadeEncode(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);
}
void Win32::WemadeDecode(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::Encryption::WemadeDecode(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);
}
void Win32::Base64Encode(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::Encryption::base64_encode(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);
}
void Win32::Base64Decode(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::Encryption::base64_decode(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);
}


////////////////////////////////////////////////////////////////////////////////////////////
Win32::Win32() {
}
Win32::~Win32() {
}
void Win32::Release() const {
	delete this;
}
void Win32::FreePtr(void** p) const {
	SK_DELETE_PTR_C(*p);
	*p = nullptr;
}
void Win32::DeletePtr(void** p) const {
	SK_DELETE_PTR(*p);
	*p = nullptr;
}
void Win32::EnumWindows(IWindowInfoArray** out) const {
	*out = nullptr;
	std::vector<Win::WindowInfo> outWindowInfos;
	Win::EnumWindows(outWindowInfos);
	if (!outWindowInfos.empty()) {
		*out = dynamic_cast<IWindowInfoArray*>(new __WindowInfoArray(outWindowInfos));
	}
}
void Win32::EnumChildWindows(const void* hProcessWnd, IWindowInfoArray** out) const {
	*out = nullptr;
	std::vector<Win::WindowInfo> outWindowInfos;
	Win::EnumChildWindows((HWND)hProcessWnd, outWindowInfos);
	if (!outWindowInfos.empty()) {
		*out = dynamic_cast<IWindowInfoArray*>(new __WindowInfoArray(outWindowInfos));
	}
}
void Win32::EnumProcessWindows(const unsigned long& ProcessId, IWindowInfoArray** out) const {
	*out = nullptr;
	std::vector<Win::WindowInfo> outWindowInfos;
	Win::EnumProcessWindows(ProcessId, outWindowInfos);
	if (!outWindowInfos.empty()) {
		*out = dynamic_cast<IWindowInfoArray*>(new __WindowInfoArray(outWindowInfos));
	}
}
void Win32::GeneratePathKey(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string result = Win::GeneratePathKeyA(std::string(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (char*)malloc(*dstLen);
		memcpy(*dst, result.data(), *dstLen);
	} while (0);
}
void Win32::GeneratePathKey(const wchar_t* src, const size_t& srcLen, wchar_t** dst, size_t* dstLen) const {
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::wstring result = Win::GeneratePathKeyW(std::wstring(src, srcLen));
		if (result.empty())
			break;
		*dstLen = result.size();
		*dst = (wchar_t*)malloc((*dstLen) * sizeof(wchar_t));
		wmemcpy(*dst, result.data(), *dstLen);
	} while (0);
}
bool Win32::ProcessCreate(
	const char* exePathname,
	const char* startParamenters,
	unsigned long* pid,
	const bool& Inheriting_parent_process /* = false*/,
	const bool& isShow /*= false*/,
	const unsigned long& wait_time /*= 10000*/) const {
	bool result = false;
	*pid = 0;
	do {
		if (!exePathname)
			break;
		if (!Win::AccessA(exePathname))
			break;
		result = Win::Process::CreateA(exePathname, startParamenters, *pid, Inheriting_parent_process, isShow, wait_time);
	} while (0);
	return result;
}
bool Win32::MBytesToUTF8(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string str = IConv::MBytesToUTF8(std::string(src, srcLen));
		if (str.empty())
			break;
		*dst = (char*)malloc(str.size());
		*dstLen = str.size();
		memcpy(*dst, str.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;
}
bool Win32::UTF8ToMBytes(const char* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string str = IConv::UTF8ToMBytes(std::string(src, srcLen));
		if (str.empty())
			break;
		*dst = (char*)malloc(str.size());
		*dstLen = str.size();
		memcpy(*dst, str.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;
}
bool Win32::UTF8ToWString(const char* src, const size_t& srcLen, wchar_t** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::wstring wstr = IConv::UTF8ToWString(std::string(src, srcLen));
		if (wstr.empty())
			break;
		*dst = (wchar_t*)malloc(wstr.size() * sizeof(wchar_t));
		*dstLen = wstr.size();
		wmemcpy(*dst, wstr.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;
}
bool Win32::MBytesToWString(const char* src, const size_t& srcLen, wchar_t** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::wstring wstr = IConv::MBytesToWString(std::string(src, srcLen));
		if (wstr.empty())
			break;
		*dst = (wchar_t*)malloc(wstr.size() * sizeof(wchar_t));
		*dstLen = wstr.size();
		wmemcpy(*dst, wstr.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;
}
bool Win32::WStringToMBytes(const wchar_t* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string u8str = IConv::WStringToMBytes(std::wstring(src, srcLen));
		if (u8str.empty())
			break;
		*dst = (char*)malloc(u8str.size());
		*dstLen = u8str.size();
		memcpy(*dst, u8str.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;
}
bool Win32::WStringToUTF8(const wchar_t* src, const size_t& srcLen, char** dst, size_t* dstLen) const {
	bool result = false;
	*dst = nullptr;
	*dstLen = 0;
	do {
		if (!src || srcLen <= 0)
			break;
		std::string u8str = IConv::WStringToUTF8(std::wstring(src, srcLen));
		if (u8str.empty())
			break;
		*dst = (char*)malloc(u8str.size());
		*dstLen = u8str.size();
		memcpy(*dst, u8str.c_str(), *dstLen);
		result = true;
	} while (0);
	return result;

}

class DirectoryElement final : public IDirectoryElement {
public:
	DirectoryElement(const std::string& path) : path_(path) {
	}
	virtual ~DirectoryElement() {
	}
	bool IsDir() const override final {
		return isdir_;
	}
	const char* Path() const override final {
		return path_.c_str();
	}
	const char* Name() const override final {
		return name_.c_str();
	}
	long long CreateTime() const override final {
		return create_time_;
	}
	long long AccessTime() const override final {
		return access_time_;
	}
	long long WriteTime() const override final {
		return write_time_;
	}
	void Release() const override final {
		delete this;
	}
public:
	long long create_time_ = 0;
	long long access_time_ = 0;
	long long write_time_ = 0;
	const std::string path_;
	std::string name_;
	bool isdir_ = false;
};
class DirectoryArray final : public IDirectoryArray {
public:
	DirectoryArray() {

	}
	virtual ~DirectoryArray() {
		for (auto& e : eles_)
			e->Release();
		eles_.clear();
	}
	void operator<<(DirectoryElement* ele) {
		eles_.emplace_back(ele);
	}
protected:
	IDirectoryElement* Next(const size_t& index) const override final {
		if (index >= eles_.size())
			return nullptr;
		return dynamic_cast<IDirectoryElement*>(eles_[index]);
	}
	size_t Total() const override final {
		return eles_.size();
	}
	void Release() const override final {
		delete this;
	}
private:
	std::vector<DirectoryElement*> eles_;
};
IDirectoryArray* Win32::EnumFolder(const char* path, const bool& recursion) const {
	DirectoryArray* result = nullptr;
	do {
		if (!path)
			break;
		if (!Win::AccessA(path))
			break;
		tfEnumFolderNode dirs, files;
		Win::EnumFoldersAndFiles(path, dirs, files, "*.*", recursion,
			[&](const std::string& sub_path, const std::string& name, const _finddata_t& f_data) {
				if (!result)
					result = new DirectoryArray();
				auto ele = new DirectoryElement(Win::PathFixedA(std::string(path) + "\\" + sub_path));
				*result << ele;
				ele->isdir_ = (f_data.attrib & _A_SUBDIR);
				ele->name_ = name;
				ele->access_time_ = f_data.time_access;
				ele->create_time_ = f_data.time_create;
				ele->write_time_ = f_data.time_write;
			});
	} while (0);
	return dynamic_cast<IDirectoryArray*>(result);
}
bool Win32::CreateFolder(const char* path) const {
	bool result = false;
	do {
		if (!path)
			break;
		result = Win::CreateDirectoryA(path);
	} while (0);
	return result;
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Win32* __gpWin32 = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
	SHARED_API void* interface_init(void* data, unsigned long len) {
		void* result = nullptr;
		do {
			if (__gpWin32)
				break;
			__gpWin32 = new Win32();
		} while (0);
		return reinterpret_cast<void*>(dynamic_cast<IWin*>(__gpWin32));
	}
	SHARED_API void interface_uninit() {
		SK_RELEASE_PTR(__gpWin32);
	}

#ifdef __cplusplus
}
#endif