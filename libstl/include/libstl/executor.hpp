#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
namespace stl {
    // Destroy on the owning thread, after producers stop. Running jobs are joined;
    // callbacks must not destroy their own executor or wait for its idle barrier.
    class BoundedExecutor final {
    public:
        enum class Shutdown
        {
            Drain,
            CancelPending
        };
        explicit BoundedExecutor(std::size_t workers = 2, std::size_t capacity = 32,
                                 Shutdown shutdown = Shutdown::Drain)
            : worker_count_(workers), capacity_(capacity), shutdown_(shutdown) {
            if (!workers || !capacity)
                throw std::invalid_argument("executor limits");
        }
        BoundedExecutor(const BoundedExecutor&) = delete;
        BoundedExecutor& operator=(const BoundedExecutor&) = delete;
        ~BoundedExecutor() {
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
                if (shutdown_ == Shutdown::CancelPending)
                    queue_.clear();
            }
            ready_.notify_all();
            for (auto& thread : workers_)
                if (thread.joinable())
                    thread.join();
        }
        bool HasCapacity() {
            std::lock_guard lock(mutex_);
            return !stopping_ && queue_.size() < capacity_;
        }
        template <class F>
        auto Submit(F&& fn, std::function<void()> completed = {})
            -> std::future<std::invoke_result_t<std::decay_t<F>>> {
            using R = std::invoke_result_t<std::decay_t<F>>;
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
            auto result = task->get_future();
            std::unique_lock lock(mutex_);
            if (stopping_)
                throw std::runtime_error("executor stopped");
            if (queue_.size() >= capacity_)
                throw std::length_error("executor full");
            if (workers_.empty()) {
                try {
                    for (std::size_t i = 0; i < worker_count_; ++i)
                        workers_.emplace_back([this] { Run(); });
                }
                catch (...) {
                    // Roll back all started workers before exposing failed admission.
                    stopping_ = true;
                    lock.unlock();
                    ready_.notify_all();
                    for (auto& thread : workers_)
                        if (thread.joinable())
                            thread.join();
                    throw;
                }
            }
            queue_.push_back([task, completed = std::move(completed)] {
                (*task)();
                if (completed) {
                    try {
                        completed();
                    }
                    catch (...) {
                    }
                }
            });
            ready_.notify_one();
            return result;
        }
        void WaitForIdle() {
            std::unique_lock lock(mutex_);
            idle_.wait(lock, [this] { return queue_.empty() && !active_; });
        }
        bool WaitForIdleFor(std::chrono::milliseconds timeout) {
            std::unique_lock lock(mutex_);
            return idle_.wait_for(lock, timeout, [this] { return queue_.empty() && !active_; });
        }

    private:
        void Run() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty())
                        return;
                    task = std::move(queue_.front());
                    queue_.pop_front();
                    ++active_;
                }
                task();
                {
                    std::lock_guard lock(mutex_);
                    --active_;
                }
                idle_.notify_all();
            }
        }
        const std::size_t worker_count_, capacity_;
        const Shutdown shutdown_;
        std::mutex mutex_;
        std::condition_variable ready_, idle_;
        std::deque<std::function<void()>> queue_;
        std::vector<std::thread> workers_;
        std::size_t active_ = 0;
        bool stopping_ = false;
    };
} // namespace stl
