#ifndef LIBUVBRG_EVENT_LOOP_H
#define LIBUVBRG_EVENT_LOOP_H

#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>
#include <chrono>
#include <optional>

namespace uvbrg {

    class EventLoop {
    public:
        using Task = std::function<void()>;

        using Observer = std::function<void(bool, const char*, const char*, int)>;
        EventLoop() = default;
        struct Limits {
            std::size_t pending_tasks = 256;
            std::size_t pending_bytes = 4 * 1024 * 1024;
            std::size_t tasks_per_turn = 64;
        };
        explicit EventLoop(Limits limits, Observer observer = {});
        virtual ~EventLoop();
        EventLoop(const EventLoop&) = delete;
        EventLoop& operator=(const EventLoop&) = delete;

        int Start();
        void Stop();
        // Payload bytes exclude the queue entry itself. Callers retaining external
        // buffers must account for them; std::function cannot report capture sizes.
        // False means not accepted. This is not a reserved completion-event channel.
        bool Post(Task task, std::size_t payload_bytes = 0);
        bool PostDelayed(Task task, std::uint64_t delay_ms);
        bool IsRunning() const;
        // One preallocated runtime pump, independent from ordinary Post capacity.
        // The owner must stop/drain producers before removing it or stopping the loop.
        bool InstallPump(Task pump);
        void RemovePump();
        bool WakePump();
        // One reusable timer for task deadlines, independent of ordinary Post quota.
        bool WakePumpAt(std::chrono::steady_clock::time_point deadline);

    private:
        struct QueuedTask {
            Task task;
            std::size_t bytes;
        };
        struct TimerRequest {
            EventLoop* owner;
            Task task;
        };

        static void OnAsync(uv_async_t* handle);
        static void OnStop(uv_async_t* handle);
        static void OnTimer(uv_timer_t* handle);
        static void OnTimerClosed(uv_handle_t* handle);

        void Invoke(Task task) noexcept;
        void Report(bool error, const char* event, const char* message, int code = 0) noexcept;
        Observer observer_;
        void Drain(bool all = false);
        void Pump();
        void CloseHandles();
        bool ScheduleTimer(Task task, std::uint64_t delay_ms);
        void ForgetTimer(uv_timer_t* timer);

        mutable std::mutex lifecycle_mutex_;
        std::condition_variable lifecycle_cv_;
        bool joining_ = false;
        std::mutex queue_mutex_;
        const Limits limits_{};
        std::deque<QueuedTask> tasks_;
        std::size_t queued_bytes_ = 0;
        std::mutex pump_mutex_;
        std::condition_variable pump_idle_;
        std::size_t active_pumps_ = 0;
        std::shared_ptr<Task> pump_;
        bool pump_pending_ = false;
        std::optional<std::chrono::steady_clock::time_point> pump_deadline_;
        std::mutex timers_mutex_;
        std::vector<uv_timer_t*> timers_;
        uv_loop_t loop_{};
        uv_async_t task_async_{};
        uv_async_t stop_async_{};
        uv_timer_t pump_timer_{};
        std::thread thread_;
        std::atomic_bool initialized_{false};
        std::atomic_bool running_{false};
        std::atomic_bool stopping_{false};
    };

} // namespace uvbrg

#endif // LIBUVBRG_EVENT_LOOP_H
