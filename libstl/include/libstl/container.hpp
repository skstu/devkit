#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// Callbacks execute under the container lock and must not reenter it.
namespace stl {
	namespace container {
		class base {
		public:
			base() = default;
			virtual ~base() = default;

		protected:
			mutable std::mutex mutex_mem_;
			mutable std::condition_variable condition_variable_mem_;
		};

		template <typename KEY, typename VAL>
		class unordered_map final : public base {
		protected:
			virtual size_t GetSize() {
				return size();
			}

		public:
			unordered_map() {
			}
			virtual ~unordered_map() {
			}

		public:
			size_t size() const {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return m_unordered_map.size();
			}
			bool push(const KEY& key, const VAL& val) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto result = m_unordered_map.emplace(std::make_pair(key, val));
				return result.second;
			}
			bool pop(const KEY& key) {
				bool result = false;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = m_unordered_map.find(key);
				if (found != m_unordered_map.end()) {
					result = true;
					m_unordered_map.erase(found);
				}
				return result;
			}
			bool search(const KEY& key,
			            const std::function<void(const VAL&)>& search_cb) const {
				bool result = false;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = m_unordered_map.find(key);
				if (found != m_unordered_map.end()) {
					result = true;
					search_cb(found->second);
				}
				return result;
			}
			void
			iterate(const std::function<void(const KEY&, VAL&, bool&)>& iterate_cb) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				bool itbreak = false;
				for (auto& node : m_unordered_map) {
					itbreak = false;
					iterate_cb(node.first, node.second, itbreak);
					if (itbreak)
						break;
				}
			}
			void iterate_clear(
			    const std::function<void(const KEY&, const VAL&, bool&)>& iterate_cb) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				bool itclear = false;
				for (auto it = m_unordered_map.begin(); it != m_unordered_map.end();) {
					itclear = false;
					iterate_cb(it->first, it->second, itclear);
					if (itclear) {
						it = m_unordered_map.erase(it);
						continue;
					}
					++it;
				}
			}
			bool empty() const {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return m_unordered_map.empty();
			}
			std::vector<VAL> Vector() const {
				std::vector<VAL> result;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				if (!m_unordered_map.empty()) {
					for (auto& node : m_unordered_map)
						result.emplace_back(node.second);
				}
				return result;
			}

		public:
			inline void operator=(const std::unordered_map<KEY, VAL>& target) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				m_unordered_map.clear();
				m_unordered_map = target;
			}

		private:
			std::unordered_map<KEY, VAL> m_unordered_map;
		};

		template <typename T = std::string>
		class queue final : public base {
		public:
			explicit queue(const size_t& _max = 0) : max_(_max) {
			}
			~queue() {
			}

		private:
			void trim_before_push() {
				if (max_ != 0 && q_.size() >= max_)
					q_.pop_front();
			}

		public:
			void push(const T& data) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				trim_before_push();
				q_.push_back(data);
			}
			void push(T&& data) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				trim_before_push();
				q_.push_back(std::move(data));
			}
			void push(const std::vector<T>& datas) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				for (const auto& data : datas) {
					trim_before_push();
					q_.push_back(data);
				}
			}
			// Return element by move. This works for non-copyable but movable types
			// (e.g. std::unique_ptr). If empty, a default-constructed T is returned.
			T pop_front() {
				T result{};
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				if (!q_.empty()) {
					result = std::move(q_.front());
					q_.pop_front();
				}
				return result;
			}

			T pop_back() {
				T result{};
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				if (!q_.empty()) {
					result = std::move(q_.back());
					q_.pop_back();
				}
				return result;
			}

			std::vector<T> pops() {
				std::vector<T> result;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				while (!q_.empty()) {
					result.emplace_back(std::move(q_.front()));
					q_.pop_front();
				}
				return result;
			}

			// Alias for pop_front
			T pop() {
				return pop_front();
			}
			bool empty() const {
				bool result = false;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				result = q_.empty();
				return result;
			}
			size_t size() const {
				size_t result = 0;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				result = q_.size();
				return result;
			}
			void clear() {
				std::deque<T> empty;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				std::swap(q_, empty);
			}

		private:
			std::deque<T> q_;
			const size_t max_;
		};

		template <typename T>
		class deque final : public base {
		public:
			explicit deque(const size_t& _max = 0) : max_(_max) {
			}
			virtual ~deque() = default;

		private:
			void trim_before_push() {
				if (max_ == 0)
					return;
				while (q_.size() >= max_) {
					q_.pop_front();
				}
			}

		public:
			void push(const T& data) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				trim_before_push();
				q_.push_back(data);
			}
			void push(T&& data) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				trim_before_push();
				q_.push_back(std::move(data));
			}
			void push(const std::vector<T>& datas) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				for (const auto& data : datas) {
					trim_before_push();
					q_.push_back(data);
				}
			}

			std::shared_ptr<T> pop_front() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				if (!q_.empty()) {
					result = std::make_shared<T>(std::move(q_.front()));
					q_.pop_front();
				}
				return result;
			}
			std::shared_ptr<T> pop_back() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				if (!q_.empty()) {
					result = std::make_shared<T>(std::move(q_.back()));
					q_.pop_back();
				}
				return result;
			}
			std::deque<T> pops() {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				std::deque<T> result;
				result.swap(q_);
				return result;
			}
			std::shared_ptr<T> pop() {
				return pop_front();
			}
			bool empty() const {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return q_.empty();
			}
			size_t size() const {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return q_.size();
			}
			void clear() {
				std::deque<T> empty;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				std::swap(q_, empty);
			}

		private:
			std::deque<T> q_;
			const size_t max_;
		};

		template <typename K, typename V>
		class map final : public base {
		public:
			map() {
			}
			virtual ~map() {
			}

		public:
			void push(const K& k, const V& v) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = map_.find(k);
				if (found != map_.end())
					map_.erase(found);
				map_.emplace(k, v);
			}
			void push(const K& k, const V& v,
			          const std::function<void(const V&)>& exists_cb) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = map_.find(k);
				if (found != map_.end()) {
					exists_cb(found->second);
					map_.erase(found);
				}
				map_.emplace(k, v);
			}
			void push(const K& k, const V& v,
			          const std::function<void(const V& old, const V& ins)>& exists_cb) {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = map_.find(k);
				if (found != map_.end()) {
					exists_cb(found->second, v);
					map_.erase(found);
				}
				map_.emplace(k, v);
			}
			std::shared_ptr<V> pop(const K& k) {
				std::shared_ptr<V> result;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = map_.find(k);
				if (found != map_.end()) {
					result = std::make_shared<V>(found->second);
					map_.erase(found);
				}
				return result;
			}
			std::shared_ptr<V> search(const K& k) const {
				std::shared_ptr<V> result = nullptr;
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				auto found = map_.find(k);
				if (found != map_.end())
					result = std::make_shared<V>(found->second);
				return result;
			}
			void clear() {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				map_.clear();
			}
			bool empty() {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return map_.empty();
			}
			size_t size() const {
				std::lock_guard<std::mutex> _lock(mutex_mem_);
				return map_.size();
			}
			void iterate(const std::function<void(const K&, V&)>& iteratecb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (auto it = map_.begin(); it != map_.end(); ++it) {
					iteratecb(it->first, it->second);
				}
			}
			void
			iterate_clear(const std::function<void(const K&, V&, bool&)>& iteratecb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (auto it = map_.begin(); it != map_.end();) {
					bool clear = false;
					iteratecb(it->first, it->second, clear);
					if (!clear) {
						++it;
						continue;
					}
					it = map_.erase(it);
				}
			}
			void iterate(
			    const std::function<void(const K&, V&, const size_t&)>& iteratecb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				size_t i = 0;
				for (auto it = map_.begin(); it != map_.end(); ++it, ++i) {
					iteratecb(it->first, it->second, i);
				}
			}
			bool search(const K& key,
			            const std::function<void(const K&, V&)>& search_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto found = map_.find(key);
				if (found == map_.end())
					return false;
				search_cb(found->first, found->second);
				return true;
			}

		private:
			std::map<K, V> map_;
		};

		template <typename T>
		class list final : public base {
		public:
			list() : m_list() {
			}
			virtual ~list() {
			}

		public:
			void operator=(const std::list<T>& target) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_list.clear();
				m_list = target;
			}
			void operator=(const stl::container::list<T>& target) {
				if (this == &target)
					return;
				std::scoped_lock lock(mutex_mem_, target.mutex_mem_);
				m_list = target.m_list;
			}

		public:
			void push_front(const T& data) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_list.push_front(data);
			}
			void push_back(const T& data) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_list.push_back(data);
			}
			std::shared_ptr<T> pop_back() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_list.empty()) {
					return result;
				}
				result = std::make_shared<T>(m_list.back());
				m_list.pop_back();
				return result;
			}
			std::shared_ptr<T> pop_front() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_list.empty()) {
					return result;
				}
				result = std::make_shared<T>(m_list.front());
				m_list.pop_front();
				return result;
			}
			std::shared_ptr<T> back() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_list.empty()) {
					result = std::make_shared<T>(m_list.back());
				}
				return result;
			}
			std::shared_ptr<T> back(const size_t& idx) {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (idx < m_list.size()) {
					auto it = m_list.begin();
					std::advance(it, m_list.size() - 1 - idx);
					result = std::make_shared<T>(*it);
				}
				return result;
			}
			std::shared_ptr<T> front() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_list.empty()) {
					result = std::make_shared<T>(m_list.front());
				}
				return result;
			}
			std::size_t size() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_list.size();
			}
			bool empty() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_list.empty();
			}
			void clear() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_list.clear();
			}
			std::shared_ptr<T> search_copy(const T& data) const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto found = std::find(m_list.begin(), m_list.end(), data);
				return found == m_list.end() ? nullptr : std::make_shared<T>(*found);
			}
			// Borrowed pointer: requires external synchronization after this call.
			T* search(const T& data) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = std::find(m_list.begin(), m_list.end(), data);
				if (find != m_list.end()) {
					return &(*find);
				}
				else {
					return nullptr;
				}
			}
			void iterate(const std::function<void(T&)>& iterate_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (auto it = m_list.begin(); it != m_list.end(); ++it) {
					iterate_cb(*it);
				}
			}
			void iterate(const std::function<void(T&, bool& clear)>& iterate_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (auto it = m_list.begin(); it != m_list.end();) {
					bool is_clear = false;
					iterate_cb(*it, is_clear);
					if (is_clear) {
						it = m_list.erase(it);
						continue;
					}
					++it;
				}
			}
			std::vector<T> Vector() const {
				std::vector<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (const auto& node : m_list) {
					result.emplace_back(node);
				}
				return result;
			}

		private:
			std::list<T> m_list;
		};

		template <typename T>
		class set final : public base {
		public:
			set() {
			}
			virtual ~set() {
			}
			void operator=(const std::set<T>& stlset) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_set = stlset;
			}
			void operator=(const set<T>& skset) {
				if (this == &skset)
					return;
				std::scoped_lock lock(mutex_mem_, skset.mutex_mem_);
				m_set = skset.m_set;
			}

		public:
			std::shared_ptr<T> begin() const {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_set.empty()) {
					return std::make_shared<T>(*m_set.begin());
				}
				return result;
			}
			std::shared_ptr<T> end() const {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_set.empty()) {
					return std::make_shared<T>(*std::prev(m_set.end()));
				}
				return result;
			}
			std::shared_ptr<T> pop_begin() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_set.empty()) {
					auto itTarget = m_set.begin();
					result = std::make_shared<T>(*itTarget);
					m_set.erase(itTarget);
				}
				return result;
			}
			std::shared_ptr<T> pop_end() {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_set.empty()) {
					auto itTarget = std::prev(m_set.end());
					result = std::make_shared<T>(*itTarget);
					m_set.erase(itTarget);
				}
				return result;
			}
			bool push(const T& data) {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_set.find(data) != m_set.end()) {
					m_set.erase(data);
					result = true;
				}
				m_set.insert(data);
				return result;
			}

			bool pushpush(const T& data, const std::function<void(T&)>& pushpush_cb) {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = m_set.find(data);
				if (find != m_set.end()) {
					auto val = *find;
					m_set.erase(find);
					pushpush_cb(val);
					result = m_set.insert(val).second;
				}
				else {
					result = m_set.insert(data).second;
				}
				return result;
			}
			bool pop(const T& key) {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = m_set.find(key);
				if (find != m_set.end()) {
					m_set.erase(find);
					result = true;
				}
				return result;
			}
			bool empty() const {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				result = m_set.empty();
				return result;
			}
			size_t size() const {
				size_t result = 0;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				result = m_set.size();
				return result;
			}
			void clear() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_set.clear();
			}
			void clearat(const std::function<void(const T&, bool&)>& iterate_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				bool isClear = false;
				for (auto it = m_set.begin(); it != m_set.end();) {
					isClear = false;
					iterate_cb(*it, isClear);
					if (true == isClear) {
						it = m_set.erase(it);
					}
					else {
						++it;
					}
				}
			}
			bool exists(const T& key) const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_set.find(key) != m_set.end();
			}
			std::shared_ptr<T> search(const T& key) {
				std::shared_ptr<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_set.empty()) {
					auto find = m_set.find(key);
					if (find != m_set.end()) {
						return std::make_shared<T>(*find);
					}
				}
				return result;
			}
			bool search(const T& key, const std::function<void(const T&)>& search_cb) {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = m_set.find(key);
				if (find != m_set.end()) {
					search_cb(*find);
					result = true;
				}
				return result;
			}
			bool search_noconst(const T& key, const std::function<void(T&)>& search_cb) {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = m_set.find(key);
				if (find != m_set.end()) {
					// std::set keys are immutable. Preserve the legacy callback shape by
					// providing a detached value; use pushpush() to replace an element.
					T value = *find;
					search_cb(value);
					result = true;
				}
				return result;
			}
			bool search(const T& key) const {
				bool result = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto find = m_set.find(key);
				if (find != m_set.end()) {
					result = true;
				}
				return result;
			}
			void iterate(const std::function<void(T&)>& iteratecb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (const auto& item : m_set) {
					// std::set keys are immutable. Keep the legacy mutable callback
					// signature without casting away that constness.
					T value = item;
					iteratecb(value);
				}
			}
			void
			iterate_const(const std::function<void(const T&, bool&)>& iteratecb) const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				bool itbreak = false;
				for (auto it = m_set.begin(); it != m_set.end(); ++it) {
					itbreak = false;
					iteratecb(*it, itbreak);
					if (itbreak)
						break;
				}
			}
			void iterate(const std::function<void(const T&, bool&)>& iteratecb) const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				bool itbreak = false;
				for (auto it = m_set.begin(); it != m_set.end(); ++it) {
					iteratecb(*it, itbreak);
					if (itbreak == true) {
						break;
					}
				}
			}
			void iterate(const std::function<void(T&, bool&)>& iteratecb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				bool itbreak = false;
				for (const auto& item : m_set) {
					// See the single-argument overload above: mutations apply to this copy.
					T value = item;
					itbreak = false;
					iteratecb(value, itbreak);
					if (itbreak == true) {
						break;
					}
				}
			}
			std::vector<T> Vector() const {
				std::vector<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (const auto& node : m_set) {
					result.emplace_back(node);
				}
				return result;
			}
			std::set<T> Source() {
				std::set<T> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (const auto& node : m_set) {
					result.insert(node);
				}
				return result;
			}

		private:
			std::set<T> m_set;
		};
		template <typename K, typename V>
		class multimap final : public base {
		private:
			size_t m_max_size = 0;

		public:
			multimap(const std::multimap<K, V>& obj) : m_map(obj.begin(), obj.end()) {
			}
			multimap(size_t max_size = 0) : m_max_size(max_size) {
			}
			virtual ~multimap() {
			}
			void SetMaxSize(const size_t& max_size) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_max_size = max_size;
			}

		public:
			bool empty() const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_map.empty();
			}
			size_t push(const K& key, const V& val) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_max_size > 0) {
					while (m_map.size() >= m_max_size) {
						m_map.erase(std::prev(m_map.end()));
					}
				}
				m_map.insert(std::make_pair(key, val));
				return m_map.size();
			}

			std::shared_ptr<std::list<V>>
			search(const std::function<bool(const V&)>& cbSerach) const {
				std::shared_ptr<std::list<V>> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				for (const auto& node : m_map) {
					if (cbSerach(node.second)) {
						if (!result) {
							result = std::make_shared<std::list<V>>();
						}
						result->push_front(node.second);
					}
					else {
						break;
					}
				}
				return result;
			}

			std::shared_ptr<std::list<V>> get(const size_t& count) {
				std::shared_ptr<std::list<V>> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (count > 0) {
					size_t pos = 0;
					for (const auto& node : m_map) {
						if (pos >= count)
							break;
						if (!result) {
							result = std::make_shared<std::list<V>>();
						}
						result->push_front(node.second);
						++pos;
					}
				}
				return result;
			}

			std::shared_ptr<std::tuple<K, V>> pop() {
				std::shared_ptr<std::tuple<K, V>> val;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return val;
				}
				auto it = m_map.begin();
				if (it == m_map.end()) {
					return val;
				}
				val = std::make_shared<std::tuple<K, V>>(std::tie(it->first, it->second));
				m_map.erase(it);
				return val;
			}
			// pop_newest: keep existing behavior (begin() for current comparator)
			std::shared_ptr<std::tuple<K, V>> pop_newest() {
				return pop();
			}

			// pop_oldest: return the element from the opposite end (oldest for descending
			// comparator)
			std::shared_ptr<std::tuple<K, V>> pop_oldest() {
				std::shared_ptr<std::tuple<K, V>> val;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty())
					return val;
				auto it = std::prev(m_map.end());
				val = std::make_shared<std::tuple<K, V>>(std::tie(it->first, it->second));
				m_map.erase(it);
				return val;
			}
			// aliases with clearer names
			std::shared_ptr<std::tuple<K, V>> lifo() {
				return pop_newest();
			}
			std::shared_ptr<std::tuple<K, V>> fifo() {
				return pop_oldest();
			}
			void pop_back(size_t count = 1) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return;
				}
				if (m_map.size() <= count) {
					m_map.clear();
					return;
				}
				do {
					if (count <= 0) {
						break;
					}
					if (m_map.empty()) {
						break;
					}
					m_map.erase(std::prev(m_map.end()));
					--count;
				} while (1);
			}

			std::shared_ptr<V> pop(const K& key) {
				std::shared_ptr<V> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto found = m_map.find(key);
				if (found == m_map.end()) {
					return result;
				}
				result = std::make_shared<V>(std::move(found->second));
				m_map.erase(found);
				return result;
			}

			std::shared_ptr<V> search(const K& key, bool itclear = false) {
				std::shared_ptr<V> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto found = m_map.find(key);
				if (found == m_map.end())
					return result;
				if (itclear) {
					result = std::make_shared<V>(std::move(found->second));
					m_map.erase(found);
				}
				else {
					result = std::make_shared<V>(found->second);
				}
				return result;
			}

			std::shared_ptr<V> search(const K& key) const {
				std::shared_ptr<V> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				auto found = m_map.find(key);
				if (found == m_map.end())
					return result;
				result = std::make_shared<V>(std::move(found->second));
				return result;
			}

			std::shared_ptr<std::tuple<K, V>>
			pop(const std::function<void(const K& _key, V& _val)>& _cb) {
				std::shared_ptr<std::tuple<K, V>> val;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return val;
				}
				auto it = m_map.begin();
				if (it == m_map.end()) {
					return val;
				}
				val = std::make_shared<std::tuple<K, V>>(std::tie(it->first, it->second));
				_cb(it->first, it->second);
				m_map.erase(it);
				return val;
			}
			bool pop(const K& key,
			         const std::function<void(const K& _key, V& _val)>& _cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return false;
				}
				auto it = m_map.find(key);
				if (it == m_map.end()) {
					return false;
				}
				_cb(it->first, it->second);
				m_map.erase(it);
				return true;
			}
			bool pop(const K& key, const V& value,
			         const std::function<void(const K&, const V&)>& pop_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return false;
				}
				auto itFind = m_map.find(key);
				if (itFind != m_map.end()) {
					for (std::size_t i = 0; i < m_map.count(key); ++i, ++itFind) {
						if (itFind->second == value) {
							if (pop_cb) {
								pop_cb(itFind->first, itFind->second);
							}
							m_map.erase(itFind);
							return true;
						}
					}
				}
				return false;
			}
			void
			riterate(const std::function<void(const K&, const V&)>& riterate_cb) const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return;
				}
				for (auto rit = m_map.rbegin(); rit != m_map.rend(); ++rit) {
					riterate_cb(rit->first, rit->second);
				}
			}

			std::vector<V> pops(const K& key) {
				std::vector<V> result;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty())
					return result;
				auto find = m_map.find(key);
				if (find != m_map.end()) {
					for (std::size_t i = 0; i < m_map.count(key); ++i, ++find) {
						result.emplace_back(find->second);
					}
					m_map.erase(key);
				}
				return result;
			}

			void iterate_clear(
			    const std::function<void(const K&, V&, bool&, bool&)>& iterate_cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (!m_map.empty()) {
					bool stop_iteration = false;
					bool erase_current = false;
					for (auto it = m_map.begin(); it != m_map.end();) {
						if (stop_iteration)
							break;
						stop_iteration = false;
						erase_current = false;
						iterate_cb(it->first, it->second, stop_iteration, erase_current);
						if (erase_current) {
							it = m_map.erase(it);
							continue;
						}
						++it;
					}
				}
			}
			void
			iterate(const std::function<void(const K&, const V&, const int& cycle_index,
			                                 bool& _iterate_break)>& cb) const {
				int cycle_index = 0;
				bool _iterate_break = false;
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return;
				}
				for (auto it = m_map.begin(); it != m_map.end(); ++it) {
					if (!_iterate_break) {
						cb(it->first, it->second, ++cycle_index, _iterate_break);
					}
					else {
						break;
					}
				}
			}
			auto count(const K& key) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_map.count(key);
			}
			void iterate(const std::function<void(const K&, V&)>& cb) {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				if (m_map.empty()) {
					return;
				}
				for (auto it = m_map.begin(); it != m_map.end(); ++it) {
					cb(it->first, it->second);
				}
			}
			unsigned long long size() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_map.size();
			}
			bool empty() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return m_map.empty();
			}
			void clear() {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				m_map.clear();
			}
			std::shared_ptr<std::multimap<K, V, std::greater<K>>> src() const {
				std::lock_guard<std::mutex> lock(mutex_mem_);
				return std::make_shared<decltype(m_map)>(m_map);
			}

		private:
			std::multimap<K, V, std::greater<K>> m_map;
		};
	} // namespace container
} // namespace stl
