#ifndef LIBSTL_BYTE_BUDGET_HPP
#define LIBSTL_BYTE_BUDGET_HPP
#include <algorithm>
#include <deque>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace stl {
    // Bounded byte reservations. Acquire uses FIFO labels; Reserve bypasses waiting labels.
    class ByteBudget final : public std::enable_shared_from_this<ByteBudget> {
    public:
        struct Lease {
            Lease() = default;
            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;
            ~Lease() {
                if (!owner)
                    return;
                std::lock_guard lock(owner->mutex_);
                owner->used_ -= bytes;
                ++owner->released_;
            }

        private:
            friend class ByteBudget;
            std::shared_ptr<ByteBudget> owner;
            std::size_t bytes = 0;
        };
        explicit ByteBudget(std::size_t limit) : limit_(limit) {
        }
        std::shared_ptr<Lease> Acquire(const std::string& id, std::size_t bytes) {
            std::lock_guard lock(mutex_);
            if (bytes > limit_)
                return {};
            if (std::find(waiters_.begin(), waiters_.end(), id) == waiters_.end()) {
                if (waiters_.size() >= 256)
                    return {};
                waiters_.push_back(id);
            }
            if (waiters_.front() != id || bytes > limit_ - used_)
                return {};
            // Construct without a temporary Lease whose destructor would debit early.
            auto lease = std::make_shared<Lease>();
            lease->owner = shared_from_this();
            lease->bytes = bytes;
            used_ += bytes;
            peak_ = std::max(peak_, used_);
            ++reserved_;
            waiters_.pop_front();
            return lease;
        }
        std::shared_ptr<Lease> Reserve(std::size_t bytes) {
            std::lock_guard lock(mutex_);
            if (bytes > limit_ - used_)
                return {};
            auto lease = std::make_shared<Lease>();
            lease->owner = shared_from_this();
            lease->bytes = bytes;
            used_ += bytes;
            peak_ = std::max(peak_, used_);
            ++reserved_;
            return lease;
        }
        void Cancel(const std::string& id) {
            std::lock_guard lock(mutex_);
            std::erase(waiters_, id);
        }
        struct Stats {
            std::size_t bytes, peak, limit, waiters;
            std::uint64_t reserved, released;
        };
        Stats Inspect() const {
            std::lock_guard lock(mutex_);
            return {used_, peak_, limit_, waiters_.size(), reserved_, released_};
        }

    private:
        mutable std::mutex mutex_;
        const std::size_t limit_;
        std::size_t used_ = 0, peak_ = 0;
        std::uint64_t reserved_ = 0, released_ = 0;
        std::deque<std::string> waiters_;
    };
}
#endif
