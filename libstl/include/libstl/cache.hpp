#pragma once

#include <any>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Singleton storage deliberately survives static destruction. It is not a DLL ABI.
class ProcessObjectCache {
public:
	static ProcessObjectCache* GetInstance() {
		static auto* instance = new ProcessObjectCache;
		return instance;
	}
	template <typename T>
	void SetShared(const std::string& key, std::shared_ptr<T> ptr) {
		std::any replacement(std::move(ptr));
		std::lock_guard<std::mutex> lock(mutex_);
		// Release the replaced object after unlocking: its destructor may reenter.
		objects_[key].swap(replacement);
	}
	template <typename T>
	std::shared_ptr<T> GetShared(const std::string& key) const {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = objects_.find(key);
		if (found != objects_.end())
			if (const auto* ptr = std::any_cast<std::shared_ptr<T>>(&found->second))
				return *ptr;
		return nullptr;
	}
	// Raw pointers are borrowed. The caller owns their lifetime and synchronization.
	template <typename T>
	void SetRaw(const std::string& key, T* ptr) {
		std::lock_guard<std::mutex> lock(mutex_);
		raw_pointers_[key] = ptr;
	}
	template <typename T>
	T* GetRaw(const std::string& key) const {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = raw_pointers_.find(key);
		if (found != raw_pointers_.end())
			if (const auto* ptr = std::any_cast<T*>(&found->second))
				return *ptr;
		return nullptr;
	}
	bool Has(const std::string& key) const {
		std::lock_guard<std::mutex> lock(mutex_);
		return objects_.contains(key) || raw_pointers_.contains(key);
	}
	void Remove(const std::string& key) {
		decltype(objects_)::node_type removed;
		std::lock_guard<std::mutex> lock(mutex_);
		removed = objects_.extract(key);
		raw_pointers_.erase(key);
	}
	void Clear() {
		decltype(objects_) removed;
		std::lock_guard<std::mutex> lock(mutex_);
		removed.swap(objects_);
		raw_pointers_.clear();
	}

private:
	ProcessObjectCache() = default;
	~ProcessObjectCache() = default;
	ProcessObjectCache(const ProcessObjectCache&) = delete;
	ProcessObjectCache& operator=(const ProcessObjectCache&) = delete;
	mutable std::mutex mutex_;
	std::map<std::string, std::any> objects_;
	std::map<std::string, std::any> raw_pointers_;
};

class ProcessCache {
public:
	using ValueType = std::variant<
	    std::monostate,                    // 空值
	    std::string,                       // 字符串
	    int,                               // 整数
	    unsigned long long,                // 无符号长整数
	    double,                            // 浮点数
	    bool,                              // 布尔值
	    std::vector<std::string>,          // 字符串列表
	    std::map<std::string, std::string> // 字符串字典
	    >;

	static ProcessCache* GetInstance() {
		static ProcessCache* instance = new ProcessCache();
		return instance;
	}

	void SetString(const std::string& key, const std::string& value) {
		Set(key, value);
	}

	void SetInt(const std::string& key, int value) {
		Set(key, value);
	}

	void SetUll(const std::string& key, unsigned long long value) {
		Set(key, value);
	}

	void SetDouble(const std::string& key, double value) {
		Set(key, value);
	}

	void SetBool(const std::string& key, bool value) {
		Set(key, value);
	}

	void SetStringList(const std::string& key, const std::vector<std::string>& value) {
		Set(key, value);
	}

	void SetStringDict(const std::string& key, const std::map<std::string, std::string>& value) {
		Set(key, value);
	}

	std::optional<std::string> GetString(const std::string& key) const {
		return Get<std::string>(key);
	}

	std::optional<int> GetInt(const std::string& key) const {
		return Get<int>(key);
	}

	std::optional<unsigned long long> GetUll(const std::string& key) const {
		return Get<unsigned long long>(key);
	}

	std::optional<double> GetDouble(const std::string& key) const {
		return Get<double>(key);
	}

	std::optional<bool> GetBool(const std::string& key) const {
		return Get<bool>(key);
	}

	std::optional<std::vector<std::string>> GetStringList(const std::string& key) const {
		return Get<std::vector<std::string>>(key);
	}

	std::optional<std::map<std::string, std::string>> GetStringDict(const std::string& key) const {
		return Get<std::map<std::string, std::string>>(key);
	}
	std::string GetStringOr(const std::string& key, const std::string& default_value) const {
		auto result = GetString(key);
		return result.value_or(default_value);
	}

	int GetIntOr(const std::string& key, int default_value) const {
		auto result = GetInt(key);
		return result.value_or(default_value);
	}

	double GetDoubleOr(const std::string& key, double default_value) const {
		auto result = GetDouble(key);
		return result.value_or(default_value);
	}

	bool GetBoolOr(const std::string& key, bool default_value) const {
		auto result = GetBool(key);
		return result.value_or(default_value);
	}

	std::vector<std::string> GetStringListOr(
	    const std::string& key,
	    const std::vector<std::string>& default_value) const {
		auto result = GetStringList(key);
		return result.value_or(default_value);
	}

	std::map<std::string, std::string> GetStringDictOr(
	    const std::string& key,
	    const std::map<std::string, std::string>& default_value) const {
		auto result = GetStringDict(key);
		return result.value_or(default_value);
	}
	bool Has(const std::string& key) const {
		std::lock_guard<std::mutex> lock(mutex_);
		return cache_.find(key) != cache_.end();
	}

	void Remove(const std::string& key) {
		std::lock_guard<std::mutex> lock(mutex_);
		cache_.erase(key);
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(mutex_);
		cache_.clear();
	}

	size_t Size() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return cache_.size();
	}

	std::vector<std::string> GetAllKeys() const {
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<std::string> keys;
		keys.reserve(cache_.size());
		for (const auto& pair : cache_) {
			keys.push_back(pair.first);
		}
		return keys;
	}

private:
	template <typename T>
	void Set(const std::string& key, const T& value) {
		std::lock_guard<std::mutex> lock(mutex_);
		cache_[key] = value;
	}
	template <typename T>
	std::optional<T> Get(const std::string& key) const {
		std::lock_guard<std::mutex> lock(mutex_);
		const auto found = cache_.find(key);
		if (found != cache_.end())
			if (const auto* value = std::get_if<T>(&found->second))
				return *value;
		return std::nullopt;
	}
	ProcessCache();
	~ProcessCache();
	ProcessCache(const ProcessCache&) = delete;
	ProcessCache& operator=(const ProcessCache&) = delete;
	mutable std::mutex mutex_;
	std::map<std::string, ValueType> cache_;
};
inline ProcessCache::ProcessCache() {
}
inline ProcessCache::~ProcessCache() {
}
