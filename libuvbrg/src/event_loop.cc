#include <libuvbrg/event_loop.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace uvbrg {

    EventLoop::EventLoop(Limits limits, Observer observer) : observer_(std::move(observer)), limits_(limits) {
        if (limits.pending_tasks == 0 || limits.pending_bytes < sizeof(QueuedTask) ||
            limits.tasks_per_turn == 0)
            throw std::invalid_argument("invalid runtime queue limits");
    }

    EventLoop::~EventLoop() {
        Stop();
    }

    int EventLoop::Start() {
        std::unique_lock<std::mutex> lock(lifecycle_mutex_);
        lifecycle_cv_.wait(lock, [this] { return !joining_; });
        if (running_.load())
            return 0;
        if (thread_.joinable()) {
            joining_ = true;
            lock.unlock();
            thread_.join();
            lock.lock();
            joining_ = false;
            lifecycle_cv_.notify_all();
        }
        stopping_.store(false);

        int status = uv_loop_init(&loop_);
        if (status != 0) {
            Report(true, "loop-init-failed", "libuv loop initialization failed", status);
            return status;
        }
        initialized_.store(true);

        task_async_.data = this;
        status = uv_async_init(&loop_, &task_async_, &EventLoop::OnAsync);
        if (status != 0) {
            Report(true, "task-async-init-failed", "libuv task dispatcher initialization failed", status);
            uv_loop_close(&loop_);
            initialized_.store(false);
            return status;
        }
        stop_async_.data = this;
        status = uv_async_init(&loop_, &stop_async_, &EventLoop::OnStop);
        if (status != 0) {
            Report(true, "stop-async-init-failed", "libuv stop dispatcher initialization failed", status);
            uv_close(reinterpret_cast<uv_handle_t*>(&task_async_), nullptr);
            uv_run(&loop_, UV_RUN_DEFAULT);
            uv_loop_close(&loop_);
            initialized_.store(false);
            return status;
        }

        pump_timer_.data = this;
        status = uv_timer_init(&loop_, &pump_timer_);
        if (status != 0) {
            uv_close(reinterpret_cast<uv_handle_t*>(&task_async_), nullptr);
            uv_close(reinterpret_cast<uv_handle_t*>(&stop_async_), nullptr);
            uv_run(&loop_, UV_RUN_DEFAULT);
            uv_loop_close(&loop_);
            initialized_.store(false);
            return status;
        }
        running_.store(true);
        Report(false, "loop-started", "libuv runtime loop started");
        try {
            thread_ = std::thread([this] {
                uv_run(&loop_, UV_RUN_DEFAULT);
                running_.store(false);
                uv_loop_close(&loop_);
                initialized_.store(false);
                Report(false, "loop-stopped", "libuv runtime loop stopped");
            });
        }
        catch (...) {
            stopping_.store(true);
            CloseHandles();
            uv_run(&loop_, UV_RUN_DEFAULT);
            uv_loop_close(&loop_);
            running_.store(false);
            initialized_.store(false);
            return UV_EAGAIN;
        }
        return 0;
    }

    void EventLoop::Stop() {
        std::unique_lock<std::mutex> lock(lifecycle_mutex_);
        if (thread_.joinable() && std::this_thread::get_id() == thread_.get_id()) {
            if (initialized_.load() && !stopping_.exchange(true)) {
                uv_async_send(&stop_async_);
            }
            return;
        }
        lifecycle_cv_.wait(lock, [this] { return !joining_; });
        if (initialized_.load() && !stopping_.exchange(true)) {
            uv_async_send(&stop_async_);
        }
        if (!thread_.joinable())
            return;
        joining_ = true;
        lock.unlock();
        thread_.join();
        lock.lock();
        joining_ = false;
        lifecycle_cv_.notify_all();
    }

    bool EventLoop::Post(Task task, std::size_t payload_bytes) {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (!task || !running_.load() || stopping_.load())
            return false;
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        if (tasks_.size() >= limits_.pending_tasks ||
            payload_bytes > limits_.pending_bytes - sizeof(QueuedTask))
            return false;
        const auto bytes = sizeof(QueuedTask) + payload_bytes;
        if (bytes > limits_.pending_bytes - queued_bytes_)
            return false;
        try {
            tasks_.push_back({std::move(task), bytes});
        }
        catch (...) {
            return false;
        }
        queued_bytes_ += bytes;
        if (uv_async_send(&task_async_) != 0) {
            queued_bytes_ -= bytes;
            tasks_.pop_back();
            return false;
        }
        return true;
    }

    bool EventLoop::PostDelayed(Task task, std::uint64_t delay_ms) {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (!task || !running_.load() || stopping_.load())
                return false;
            if (std::this_thread::get_id() == thread_.get_id()) {
                return ScheduleTimer(std::move(task), delay_ms);
            }
        }
        return Post([this, task = std::move(task), delay_ms]() mutable {
            ScheduleTimer(std::move(task), delay_ms);
        });
    }

    bool EventLoop::IsRunning() const {
        return running_.load();
    }

    bool EventLoop::InstallPump(Task pump) {
        if (!pump)
            return false;
        auto owned = std::make_shared<Task>(std::move(pump));
        std::lock_guard lock(pump_mutex_);
        if (pump_)
            return false;
        pump_ = std::move(owned);
        return true;
    }

    void EventLoop::RemovePump() {
        bool on_loop;
        {
            std::lock_guard lifecycle(lifecycle_mutex_);
            on_loop = thread_.get_id() == std::this_thread::get_id();
        }
        std::unique_lock lock(pump_mutex_);
        pump_.reset();
        pump_pending_ = false;
        pump_deadline_.reset();
        // Removing the pump is also a barrier for its entire invocation, including
        // its tail after task finalizers and observers have drained.
        if (!on_loop)
            pump_idle_.wait(lock, [this] { return active_pumps_ == 0; });
    }

    bool EventLoop::WakePump() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        if (!running_.load() || stopping_.load())
            return false;
        std::lock_guard lock(pump_mutex_);
        if (!pump_)
            return false;
        pump_pending_ = true;
        return uv_async_send(&task_async_) == 0;
    }

    bool EventLoop::WakePumpAt(std::chrono::steady_clock::time_point deadline) {
        std::lock_guard lifecycle(lifecycle_mutex_);
        if (!running_.load() || stopping_.load())
            return false;
        std::lock_guard lock(pump_mutex_);
        if (!pump_)
            return false;
        if (!pump_deadline_ || deadline < *pump_deadline_)
            pump_deadline_ = deadline;
        return uv_async_send(&task_async_) == 0;
    }

    void EventLoop::Pump() {
        std::shared_ptr<Task> pump;
        {
            std::lock_guard lock(pump_mutex_);
            const auto now = std::chrono::steady_clock::now();
            if (pump_deadline_ && now >= *pump_deadline_) {
                pump_pending_ = true;
                pump_deadline_.reset();
            }
            if (pump_deadline_) {
                const auto delay = std::chrono::ceil<std::chrono::milliseconds>(*pump_deadline_ - now).count();
                uv_timer_start(&pump_timer_, [](uv_timer_t* timer) { static_cast<EventLoop*>(timer->data)->Pump(); }, static_cast<std::uint64_t>(std::max<std::int64_t>(1, delay)), 0);
            }
            else
                uv_timer_stop(&pump_timer_);
            if (!pump_pending_)
                return;
            pump_pending_ = false;
            pump = pump_;
            if (pump)
                ++active_pumps_;
        }
        if (pump) {
            try {
                (*pump)();
            }
            catch (...) {
                Report(true, "pump-failed", "runtime pump threw an exception");
            }
            {
                std::lock_guard lock(pump_mutex_);
                --active_pumps_;
            }
            pump_idle_.notify_all();
        }
    }

    void EventLoop::OnAsync(uv_async_t* handle) {
        auto* self = static_cast<EventLoop*>(handle->data);
        self->Drain();
        self->Pump();
    }

    void EventLoop::OnStop(uv_async_t* handle) {
        auto* self = static_cast<EventLoop*>(handle->data);
        // Stop has already closed admission. Drain the finite accepted queue before
        // closing the wake handle; the ordinary per-turn budget must not lose work.
        self->Drain(true);
        self->Pump();
        self->CloseHandles();
    }

    void EventLoop::OnTimer(uv_timer_t* handle) {
        auto* request = static_cast<TimerRequest*>(handle->data);
        request->owner->ForgetTimer(handle);
        Task task = std::move(request->task);
        auto* request_owner = request->owner;
        delete request;
        handle->data = nullptr;
        uv_close(reinterpret_cast<uv_handle_t*>(handle),
                 &EventLoop::OnTimerClosed);
        request_owner->Invoke(std::move(task));
    }

    void EventLoop::OnTimerClosed(uv_handle_t* handle) {
        delete static_cast<TimerRequest*>(handle->data);
        delete reinterpret_cast<uv_timer_t*>(handle);
    }

    void EventLoop::Invoke(Task task) noexcept {
        try {
            task();
        }
        catch (...) {
            // Callback exceptions cannot unwind through libuv C callbacks.
            Report(true, "callback-failed", "runtime callback threw an exception");
        }
    }

    void EventLoop::Drain(bool all) {
        for (std::size_t count = 0; all || count < limits_.tasks_per_turn; ++count) {
            Task task;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (tasks_.empty())
                    break;
                auto& entry = tasks_.front();
                task = std::move(entry.task);
                queued_bytes_ -= entry.bytes;
                tasks_.pop_front();
            }
            Invoke(std::move(task));
        }
        // uv_async_send coalesces wakeups. Explicitly reschedule leftover work so
        // timers and I/O can run between finite batches, even without new producers.
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!tasks_.empty())
            uv_async_send(&task_async_);
    }

    void EventLoop::CloseHandles() {
        uv_timer_stop(&pump_timer_);
        uv_close(reinterpret_cast<uv_handle_t*>(&pump_timer_), nullptr);
        std::vector<uv_timer_t*> timers;
        {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            timers.swap(timers_);
        }
        for (uv_timer_t* timer : timers) {
            if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(timer))) {
                uv_close(reinterpret_cast<uv_handle_t*>(timer),
                         &EventLoop::OnTimerClosed);
            }
        }
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&task_async_))) {
            uv_close(reinterpret_cast<uv_handle_t*>(&task_async_), nullptr);
        }
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&stop_async_))) {
            uv_close(reinterpret_cast<uv_handle_t*>(&stop_async_), nullptr);
        }
    }

    bool EventLoop::ScheduleTimer(Task task, std::uint64_t delay_ms) {
        auto* timer = new uv_timer_t{};
        auto* request = new TimerRequest{this, std::move(task)};
        timer->data = request;
        int status = uv_timer_init(&loop_, timer);
        if (status != 0) {
            delete request;
            delete timer;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            timers_.push_back(timer);
        }
        status = uv_timer_start(timer, &EventLoop::OnTimer, delay_ms, 0);
        if (status != 0) {
            ForgetTimer(timer);
            uv_close(reinterpret_cast<uv_handle_t*>(timer),
                     &EventLoop::OnTimerClosed);
            return false;
        }
        return true;
    }

    void EventLoop::ForgetTimer(uv_timer_t* timer) {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        const auto it = std::find(timers_.begin(), timers_.end(), timer);
        if (it != timers_.end())
            timers_.erase(it);
    }

} // namespace uvbrg

void uvbrg::EventLoop::Report(bool error, const char* event, const char* message, int code) noexcept {
    if (observer_) {
        try {
            observer_(error, event, message, code);
        }
        catch (...) {
        }
    }
}
