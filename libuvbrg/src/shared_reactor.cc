#include "shared_reactor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(min)
#undef min
#endif
#if defined(max)
#undef max
#endif

namespace uvbrg::internal {
namespace {

constexpr size_t kMaxReactors = 4;
constexpr size_t kMaxQueuedTasks = 4096;
constexpr size_t kMaxQueuedBytes = 16 * 1024 * 1024;
constexpr size_t kMaxPerKeyTasks = 256;
constexpr size_t kMaxPerKeyBytes = 2 * 1024 * 1024;
constexpr size_t kReservedControlTasks = 64;
constexpr size_t kReservedControlBytes = 1024 * 1024;
constexpr size_t kDrainBatch = 256;

#ifdef LIBUVBRG_TESTING
constexpr uint32_t kNoFaultLane = std::numeric_limits<uint32_t>::max();
std::atomic<uint32_t> g_loop_close_fault_lane{kNoFaultLane};
std::atomic<uint32_t> g_loop_close_fault_remaining{0};
std::atomic<int> g_loop_close_fault_error{UV_EBUSY};
std::atomic<uint64_t> g_shutdown_request_captures{0};

struct ShutdownPauseState {
  bool armed = false;
  bool waiting = false;
  bool released = false;
};

struct ShutdownPauseGate {
  std::mutex mtx;
  std::condition_variable cv;
  ShutdownPauseState before_close;
  ShutdownPauseState after_publish;
};

ShutdownPauseGate &GetShutdownPauseGate() {
  static ShutdownPauseGate *gate = new ShutdownPauseGate();
  return *gate;
}

ShutdownPauseState &SelectShutdownPause(
    ShutdownPauseGate &gate, testing::ShutdownPausePoint point) {
  return point == testing::ShutdownPausePoint::BeforeClose
             ? gate.before_close
             : gate.after_publish;
}

void MaybePauseShutdown(testing::ShutdownPausePoint point) {
  ShutdownPauseGate &gate = GetShutdownPauseGate();
  std::unique_lock<std::mutex> lk(gate.mtx);
  ShutdownPauseState &state = SelectShutdownPause(gate, point);
  if (!state.armed)
    return;
  state.armed = false;
  state.waiting = true;
  gate.cv.notify_all();
  gate.cv.wait(lk, [&]() { return state.released; });
  state.waiting = false;
  state.released = false;
}

#endif

thread_local const void *g_lane = nullptr;

uint64_t StableHash(const std::string &value) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : value) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return hash;
}

size_t ReactorCount(size_t requested) {
  if (requested != 0)
    return std::max<size_t>(1, std::min(kMaxReactors, requested));
  const unsigned cores = std::thread::hardware_concurrency();
  if (cores <= 2)
    return 1;
  return std::max<size_t>(
      1, std::min(kMaxReactors, static_cast<size_t>(cores / 2)));
}

size_t Add(size_t lhs, size_t rhs) {
  return rhs > std::numeric_limits<size_t>::max() - lhs
             ? std::numeric_limits<size_t>::max()
             : lhs + rhs;
}

int CloseLoop([[maybe_unused]] uint32_t lane_index, uv_loop_t *loop) {
#ifdef LIBUVBRG_TESTING
  if (g_loop_close_fault_lane.load(std::memory_order_acquire) == lane_index) {
    uint32_t remaining =
        g_loop_close_fault_remaining.load(std::memory_order_acquire);
    while (remaining != 0) {
      if (g_loop_close_fault_remaining.compare_exchange_weak(
              remaining, remaining - 1, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
        return g_loop_close_fault_error.load(std::memory_order_acquire);
      }
    }
  }
#endif
  return uv_loop_close(loop);
}

class Lane : public std::enable_shared_from_this<Lane> {
public:
  enum class JoinStatus {
    Closed,
    CloseFailed,
    TimedOut,
    CalledFromReactorThread,
  };

  struct Task {
    uint64_t id = 0;
    std::string key;
    size_t bytes = 0;
    bool control = false;
    std::function<void(uv_loop_t *)> run;
    std::function<void()> cancel;
  };

  explicit Lane(uint32_t index) : index_(index) {}
  ~Lane() {
    if (thread_.joinable())
      thread_.detach();
  }

  bool Start(std::string *error) {
    auto self = shared_from_this();
    try {
      thread_ =
          std::thread([self = std::move(self)]() { self->ThreadMain(); });
    } catch (...) {
      if (error)
        *error = "proxy shared reactor thread start failed";
      std::lock_guard<std::mutex> lk(state_mtx_);
      ready_ = true;
      ready_ok_ = false;
      finished_ = true;
      ready_error_ = error ? *error : "proxy shared reactor thread start failed";
      return false;
    }
    std::unique_lock<std::mutex> lk(state_mtx_);
    if (!state_cv_.wait_for(lk, std::chrono::seconds(5),
                            [this]() { return ready_ || finished_; })) {
      if (error)
        *error = "proxy shared reactor start timeout";
      lk.unlock();
      RequestStop();
      return false;
    }
    if (!ready_ok_) {
      if (error)
        *error = ready_error_;
      lk.unlock();
      (void)WaitAndJoin(std::chrono::seconds(5));
      return false;
    }
    return true;
  }

  bool Post(Task task) {
    if (!task.run || !accepting_.load(std::memory_order_acquire))
      return false;
    task.id = next_id_.fetch_add(1, std::memory_order_relaxed);
    const uint64_t task_id = task.id;
    {
      std::lock_guard<std::mutex> lk(queue_mtx_);
      Usage &usage = per_key_[task.key];
      const size_t task_limit =
          task.control ? kMaxQueuedTasks
                       : kMaxQueuedTasks - kReservedControlTasks;
      const size_t byte_limit =
          task.control ? kMaxQueuedBytes
                       : kMaxQueuedBytes - kReservedControlBytes;
      if (tasks_.size() >= task_limit ||
          Add(queued_bytes_, task.bytes) > byte_limit ||
          usage.count >= kMaxPerKeyTasks ||
          Add(usage.bytes, task.bytes) > kMaxPerKeyBytes) {
        if (usage.count == 0 && usage.bytes == 0)
          per_key_.erase(task.key);
        rejections_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      ++usage.count;
      usage.bytes += task.bytes;
      queued_bytes_ += task.bytes;
      tasks_.push_back(std::move(task));
    }
    const int rc = wake_initialized_.load(std::memory_order_acquire)
                       ? uv_async_send(&wake_)
                       : UV_ECANCELED;
    if (rc == 0)
      return true;
    post_failures_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(queue_mtx_);
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
      if (it->id != task_id)
        continue;
      ReleaseLocked(*it);
      tasks_.erase(it);
      break;
    }
    return false;
  }

  bool IsLoopThread() const { return g_lane == this; }
  uint32_t index() const { return index_; }

  void RequestStop() {
    if (stop_requested_.exchange(true))
      return;
    Task task;
    task.key = "__shutdown__";
    task.control = true;
    task.run = [this](uv_loop_t *) {
      accepting_.store(false, std::memory_order_release);
      CancelQueued();
      if (wake_initialized_.exchange(false) &&
          !uv_is_closing(reinterpret_cast<uv_handle_t *>(&wake_))) {
        uv_close(reinterpret_cast<uv_handle_t *>(&wake_), nullptr);
      }
    };
    if (!Post(std::move(task)))
      post_failures_.fetch_add(1, std::memory_order_relaxed);
  }

  bool RequestCloseRetry() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    if (finished_ || !close_failed_ || close_retry_requested_)
      return false;
    close_failed_ = false;
    close_retry_requested_ = true;
    ++close_retry_attempts_;
    state_cv_.notify_all();
    return true;
  }

  JoinStatus WaitAndJoin(std::chrono::milliseconds timeout) {
    if (IsLoopThread())
      return JoinStatus::CalledFromReactorThread;
    {
      std::unique_lock<std::mutex> lk(state_mtx_);
      const auto terminal = [this]() { return finished_ || close_failed_; };
      const bool signaled = timeout == std::chrono::milliseconds::max()
                                ? (state_cv_.wait(lk, terminal), true)
                                : state_cv_.wait_for(lk, timeout, terminal);
      if (!signaled)
        return JoinStatus::TimedOut;
      if (close_failed_)
        return JoinStatus::CloseFailed;
    }
    if (thread_.joinable())
      thread_.join();
    return JoinStatus::Closed;
  }

  SharedLoopPoolSnapshot Snapshot() const {
    SharedLoopPoolSnapshot snapshot;
    {
      std::lock_guard<std::mutex> lk(queue_mtx_);
      snapshot.queued_tasks = tasks_.size();
      snapshot.queued_bytes = queued_bytes_;
    }
    snapshot.rejected_tasks = rejections_.load(std::memory_order_relaxed);
    snapshot.post_failures = post_failures_.load(std::memory_order_relaxed);
    snapshot.loop_close_failures =
        loop_close_failures_.load(std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lk(state_mtx_);
      snapshot.close_retry_attempts = close_retry_attempts_;
      snapshot.close_retry_successes = close_retry_successes_;
      snapshot.retained_lanes =
          loop_initialized_ && !finished_ &&
                  (close_failed_ ||
                   stop_requested_.load(std::memory_order_acquire))
              ? 1u
              : 0u;
      snapshot.final_loop_close_rc = final_loop_close_rc_;
    }
    snapshot.accepting = accepting_.load(std::memory_order_acquire);
    return snapshot;
  }

private:
  struct Usage {
    size_t count = 0;
    size_t bytes = 0;
  };

  void ReleaseLocked(const Task &task) {
    queued_bytes_ = task.bytes > queued_bytes_ ? 0
                                              : queued_bytes_ - task.bytes;
    auto it = per_key_.find(task.key);
    if (it == per_key_.end())
      return;
    if (it->second.count > 0)
      --it->second.count;
    it->second.bytes = task.bytes > it->second.bytes
                           ? 0
                           : it->second.bytes - task.bytes;
    if (it->second.count == 0 && it->second.bytes == 0)
      per_key_.erase(it);
  }

  bool Pop(Task *task) {
    std::lock_guard<std::mutex> lk(queue_mtx_);
    if (tasks_.empty())
      return false;
    *task = std::move(tasks_.front());
    tasks_.pop_front();
    ReleaseLocked(*task);
    return true;
  }

  bool HasTasks() const {
    std::lock_guard<std::mutex> lk(queue_mtx_);
    return !tasks_.empty();
  }

  void Drain() {
    Task task;
    size_t count = 0;
    while (count < kDrainBatch && Pop(&task)) {
      try {
        task.run(&loop_);
      } catch (...) {
        if (task.cancel)
          task.cancel();
      }
      task = {};
      ++count;
    }
    if (HasTasks() && wake_initialized_.load(std::memory_order_acquire))
      uv_async_send(&wake_);
  }

  void CancelQueued() {
    std::deque<Task> tasks;
    {
      std::lock_guard<std::mutex> lk(queue_mtx_);
      tasks.swap(tasks_);
      per_key_.clear();
      queued_bytes_ = 0;
    }
    for (auto &task : tasks) {
      if (task.cancel)
        task.cancel();
    }
  }

  static void OnWake(uv_async_t *handle) {
    auto *self = static_cast<Lane *>(handle->data);
    if (self)
      self->Drain();
  }

  int AttemptLoopClose() {
    int rc = CloseLoop(index_, &loop_);
    if (rc == 0)
      return 0;
    loop_close_failures_.fetch_add(1, std::memory_order_relaxed);
    uv_walk(&loop_,
            [](uv_handle_t *handle, void *) {
              if (!uv_is_closing(handle))
                uv_close(handle, nullptr);
            },
            nullptr);
    // Drain only ready callbacks. A permanently active request must become an
    // observable close failure instead of pinning the shutdown thread here.
    for (size_t turn = 0; turn < 8; ++turn) {
      if (uv_run(&loop_, UV_RUN_NOWAIT) == 0)
        break;
    }
    rc = CloseLoop(index_, &loop_);
    if (rc != 0)
      loop_close_failures_.fetch_add(1, std::memory_order_relaxed);
    return rc;
  }

  void CloseLoopWithRetry() {
    bool retry = false;
    for (;;) {
      const int rc = AttemptLoopClose();
      std::unique_lock<std::mutex> lk(state_mtx_);
      final_loop_close_rc_ = rc;
      if (retry && rc == 0)
        ++close_retry_successes_;
      if (rc == 0) {
        loop_initialized_ = false;
        close_failed_ = false;
        finished_ = true;
        lk.unlock();
        g_lane = nullptr;
        state_cv_.notify_all();
        return;
      }

      close_failed_ = true;
      state_cv_.notify_all();
      state_cv_.wait(lk, [this]() { return close_retry_requested_; });
      close_retry_requested_ = false;
      retry = true;
    }
  }

  void ThreadMain() {
    g_lane = this;
    int rc = uv_loop_init(&loop_);
    if (rc == 0) {
      {
        std::lock_guard<std::mutex> lk(state_mtx_);
        loop_initialized_ = true;
      }
      wake_.data = this;
      rc = uv_async_init(&loop_, &wake_, &Lane::OnWake);
      wake_initialized_.store(rc == 0, std::memory_order_release);
    }
    {
      std::lock_guard<std::mutex> lk(state_mtx_);
      ready_ = true;
      ready_ok_ = rc == 0;
      if (rc != 0)
        ready_error_ = std::string("proxy shared reactor init: ") +
                       uv_strerror(rc);
    }
    state_cv_.notify_all();
    if (rc != 0) {
      bool loop_initialized = false;
      {
        std::lock_guard<std::mutex> lk(state_mtx_);
        loop_initialized = loop_initialized_;
      }
      if (loop_initialized)
        CloseLoopWithRetry();
      else
        Finish();
      return;
    }
    accepting_.store(true, std::memory_order_release);
    if (stop_requested_.load(std::memory_order_acquire)) {
      accepting_.store(false, std::memory_order_release);
      if (wake_initialized_.exchange(false) &&
          !uv_is_closing(reinterpret_cast<uv_handle_t *>(&wake_))) {
        uv_close(reinterpret_cast<uv_handle_t *>(&wake_), nullptr);
      }
    }
    uv_run(&loop_, UV_RUN_DEFAULT);
    accepting_.store(false, std::memory_order_release);
    CancelQueued();
    CloseLoopWithRetry();
  }

  void Finish() {
    g_lane = nullptr;
    {
      std::lock_guard<std::mutex> lk(state_mtx_);
      finished_ = true;
    }
    state_cv_.notify_all();
  }

  uint32_t index_ = 0;
  uv_loop_t loop_{};
  uv_async_t wake_{};
  std::atomic_bool wake_initialized_{false};
  std::atomic_bool accepting_{false};
  std::atomic_bool stop_requested_{false};
  std::thread thread_;
  mutable std::mutex state_mtx_;
  std::condition_variable state_cv_;
  bool ready_ = false;
  bool ready_ok_ = false;
  bool finished_ = false;
  bool loop_initialized_ = false;
  bool close_failed_ = false;
  bool close_retry_requested_ = false;
  int final_loop_close_rc_ = 0;
  uint64_t close_retry_attempts_ = 0;
  uint64_t close_retry_successes_ = 0;
  std::string ready_error_;
  mutable std::mutex queue_mtx_;
  std::deque<Task> tasks_;
  std::unordered_map<std::string, Usage> per_key_;
  size_t queued_bytes_ = 0;
  std::atomic<uint64_t> next_id_{1};
  std::atomic<uint64_t> rejections_{0};
  std::atomic<uint64_t> post_failures_{0};
  std::atomic<uint64_t> loop_close_failures_{0};
};

struct Runtime {
  std::timed_mutex shutdown_mtx;
  std::mutex mtx;
  std::vector<std::shared_ptr<Lane>> lanes;
  size_t configured_count = 0;
  uint64_t generation = 0;
  uint64_t active_leases = 0;
  bool accepting = false;
  bool quarantined = false;
  uint64_t last_loop_close_failures = 0;
  uint64_t last_close_retry_attempts = 0;
  uint64_t last_close_retry_successes = 0;
  uint32_t last_retained_lanes = 0;
  int last_final_loop_close_rc = 0;
};

SharedLoopPoolSnapshot
AggregateLaneSnapshots(const std::vector<std::shared_ptr<Lane>> &lanes) {
  SharedLoopPoolSnapshot snapshot;
  snapshot.reactor_count = static_cast<uint32_t>(lanes.size());
  for (const auto &lane : lanes) {
    const auto lane_snapshot = lane->Snapshot();
    snapshot.queued_tasks += lane_snapshot.queued_tasks;
    snapshot.queued_bytes += lane_snapshot.queued_bytes;
    snapshot.rejected_tasks += lane_snapshot.rejected_tasks;
    snapshot.post_failures += lane_snapshot.post_failures;
    snapshot.loop_close_failures += lane_snapshot.loop_close_failures;
    snapshot.close_retry_attempts += lane_snapshot.close_retry_attempts;
    snapshot.close_retry_successes += lane_snapshot.close_retry_successes;
    snapshot.retained_lanes += lane_snapshot.retained_lanes;
    if (snapshot.final_loop_close_rc == 0 &&
        lane_snapshot.final_loop_close_rc != 0) {
      snapshot.final_loop_close_rc = lane_snapshot.final_loop_close_rc;
    }
  }
  return snapshot;
}

void ResetShutdownDiagnostics(Runtime &runtime) {
  runtime.last_loop_close_failures = 0;
  runtime.last_close_retry_attempts = 0;
  runtime.last_close_retry_successes = 0;
  runtime.last_retained_lanes = 0;
  runtime.last_final_loop_close_rc = 0;
}

void RecordShutdownDiagnostics(Runtime &runtime,
                               const SharedLoopPoolSnapshot &snapshot) {
  runtime.last_loop_close_failures = snapshot.loop_close_failures;
  runtime.last_close_retry_attempts = snapshot.close_retry_attempts;
  runtime.last_close_retry_successes = snapshot.close_retry_successes;
  runtime.last_retained_lanes = snapshot.retained_lanes;
  runtime.last_final_loop_close_rc = snapshot.final_loop_close_rc;
}

Runtime &GetRuntime() {
  static Runtime *runtime = new Runtime();
  return *runtime;
}

} // namespace

struct SharedLoopLease::Impl {
  std::shared_ptr<Lane> lane;
  std::string routing_key;
  std::atomic_bool released{false};
};

SharedLoopLease::SharedLoopLease(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SharedLoopLease::~SharedLoopLease() { Release(); }

bool SharedLoopLease::Post(size_t estimated_bytes,
                           std::function<void(uv_loop_t *)> task,
                           std::function<void()> on_cancel) {
  if (!impl_ || impl_->released.load(std::memory_order_acquire) ||
      !impl_->lane) {
    return false;
  }
  Lane::Task item;
  item.key = impl_->routing_key;
  item.bytes = estimated_bytes;
  item.run = std::move(task);
  item.cancel = std::move(on_cancel);
  return impl_->lane->Post(std::move(item));
}

bool SharedLoopLease::IsLoopThread() const {
  return impl_ && impl_->lane && impl_->lane->IsLoopThread();
}

uint32_t SharedLoopLease::reactor_index() const {
  return impl_ && impl_->lane ? impl_->lane->index() : 0;
}

const std::string &SharedLoopLease::routing_key() const {
  static const std::string empty;
  return impl_ ? impl_->routing_key : empty;
}

void SharedLoopLease::Release() {
  if (!impl_ || impl_->released.exchange(true))
    return;
  Runtime &runtime = GetRuntime();
  std::lock_guard<std::mutex> lk(runtime.mtx);
  if (runtime.active_leases > 0)
    --runtime.active_leases;
}

std::shared_ptr<SharedLoopLease>
AcquireSharedLoop(const std::string &routing_key, size_t reactor_count,
                  std::string *error) {
  Runtime &runtime = GetRuntime();
  std::lock_guard<std::mutex> lk(runtime.mtx);
  if (runtime.lanes.empty()) {
    ResetShutdownDiagnostics(runtime);
    ++runtime.generation;
    runtime.configured_count = ReactorCount(reactor_count);
    bool start_failed = false;
    try {
      for (size_t i = 0; i < runtime.configured_count; ++i) {
        auto lane = std::make_shared<Lane>(static_cast<uint32_t>(i));
        runtime.lanes.push_back(lane);
        if (!lane->Start(error)) {
          start_failed = true;
          break;
        }
      }
    } catch (...) {
      start_failed = true;
      if (error)
        *error = "proxy shared reactor allocation failed";
    }
    if (start_failed) {
      for (const auto &started : runtime.lanes) {
        if (!started->RequestCloseRetry())
          started->RequestStop();
      }
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      bool all_closed = true;
      for (const auto &started : runtime.lanes) {
        const auto now = std::chrono::steady_clock::now();
        const auto wait =
            now >= deadline
                ? std::chrono::milliseconds(0)
                : std::chrono::duration_cast<std::chrono::milliseconds>(
                      deadline - now);
        all_closed = started->WaitAndJoin(wait) == Lane::JoinStatus::Closed &&
                     all_closed;
      }
      const auto snapshot = AggregateLaneSnapshots(runtime.lanes);
      RecordShutdownDiagnostics(runtime, snapshot);
      runtime.accepting = false;
      runtime.quarantined = !all_closed;
      if (all_closed) {
        runtime.lanes.clear();
        runtime.configured_count = 0;
      } else if (error) {
        *error += "; proxy shared reactor cleanup quarantined";
      }
      return nullptr;
    }
    runtime.accepting = true;
    runtime.quarantined = false;
  }
  if (!runtime.accepting || runtime.lanes.empty()) {
    if (error)
      *error = "proxy shared reactor is not accepting leases";
    return nullptr;
  }
  const std::string key = routing_key.empty() ? "proxy-unknown" : routing_key;
  const size_t index =
      static_cast<size_t>(StableHash(key) % runtime.lanes.size());
  auto impl = std::make_unique<SharedLoopLease::Impl>();
  impl->lane = runtime.lanes[index];
  impl->routing_key = key;
  ++runtime.active_leases;
  return std::shared_ptr<SharedLoopLease>(
      new SharedLoopLease(std::move(impl)));
}

SharedLoopPoolSnapshot SharedLoopDiagnostics() {
  Runtime &runtime = GetRuntime();
  std::lock_guard<std::mutex> lk(runtime.mtx);
  SharedLoopPoolSnapshot snapshot;
  snapshot.reactor_count = static_cast<uint32_t>(runtime.lanes.size());
  snapshot.active_leases = runtime.active_leases;
  snapshot.accepting = runtime.accepting;
  snapshot.quarantined = runtime.quarantined;
  if (runtime.lanes.empty()) {
    snapshot.loop_close_failures = runtime.last_loop_close_failures;
    snapshot.close_retry_attempts = runtime.last_close_retry_attempts;
    snapshot.close_retry_successes = runtime.last_close_retry_successes;
    snapshot.retained_lanes = runtime.last_retained_lanes;
    snapshot.final_loop_close_rc = runtime.last_final_loop_close_rc;
  } else {
    const auto lane_snapshot = AggregateLaneSnapshots(runtime.lanes);
    snapshot.queued_tasks = lane_snapshot.queued_tasks;
    snapshot.queued_bytes = lane_snapshot.queued_bytes;
    snapshot.rejected_tasks = lane_snapshot.rejected_tasks;
    snapshot.post_failures = lane_snapshot.post_failures;
    snapshot.loop_close_failures = lane_snapshot.loop_close_failures;
    snapshot.close_retry_attempts = lane_snapshot.close_retry_attempts;
    snapshot.close_retry_successes = lane_snapshot.close_retry_successes;
    snapshot.retained_lanes = lane_snapshot.retained_lanes;
    snapshot.final_loop_close_rc = lane_snapshot.final_loop_close_rc;
  }
  return snapshot;
}

SharedLoopShutdownStatus
ShutdownSharedLoops(std::chrono::milliseconds timeout) {
  Runtime &runtime = GetRuntime();
  if (timeout.count() < 0)
    timeout = std::chrono::milliseconds(0);
  const bool infinite = timeout == std::chrono::milliseconds::max();
  const auto deadline = infinite
                            ? std::chrono::steady_clock::time_point::max()
                            : std::chrono::steady_clock::now() + timeout;

  uint64_t requested_generation = 0;
  {
    std::lock_guard<std::mutex> lk(runtime.mtx);
    if (runtime.lanes.empty())
      return SharedLoopShutdownStatus::AlreadyStopped;
    for (const auto &lane : runtime.lanes) {
      if (lane->IsLoopThread())
        return SharedLoopShutdownStatus::CalledFromReactorThread;
    }
    requested_generation = runtime.generation;
  }
#ifdef LIBUVBRG_TESTING
  g_shutdown_request_captures.fetch_add(1, std::memory_order_release);
#endif

  std::unique_lock<std::timed_mutex> shutdown_lk(runtime.shutdown_mtx,
                                                  std::defer_lock);
  if (infinite) {
    shutdown_lk.lock();
  } else if (!shutdown_lk.try_lock_until(deadline)) {
    return SharedLoopShutdownStatus::TimedOut;
  }

  std::vector<std::shared_ptr<Lane>> lanes;
  {
    std::lock_guard<std::mutex> lk(runtime.mtx);
    if (runtime.lanes.empty() || runtime.generation != requested_generation)
      return SharedLoopShutdownStatus::AlreadyStopped;
    for (const auto &lane : runtime.lanes) {
      if (lane->IsLoopThread())
        return SharedLoopShutdownStatus::CalledFromReactorThread;
    }
    if (runtime.active_leases != 0)
      return SharedLoopShutdownStatus::Busy;
    runtime.accepting = false;
    lanes = runtime.lanes;
  }
#ifdef LIBUVBRG_TESTING
  MaybePauseShutdown(testing::ShutdownPausePoint::BeforeClose);
#endif

  for (const auto &lane : lanes) {
    if (!lane->RequestCloseRetry())
      lane->RequestStop();
  }

  bool timed_out = false;
  bool close_failed = false;
  bool called_from_reactor = false;
  for (const auto &lane : lanes) {
    const auto now = std::chrono::steady_clock::now();
    const auto wait =
        infinite
            ? std::chrono::milliseconds::max()
            : now >= deadline
                  ? std::chrono::milliseconds(0)
                  : std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - now);
    switch (lane->WaitAndJoin(wait)) {
    case Lane::JoinStatus::Closed:
      break;
    case Lane::JoinStatus::CloseFailed:
      close_failed = true;
      break;
    case Lane::JoinStatus::TimedOut:
      timed_out = true;
      break;
    case Lane::JoinStatus::CalledFromReactorThread:
      called_from_reactor = true;
      break;
    }
  }

  const auto snapshot = AggregateLaneSnapshots(lanes);
  {
    std::lock_guard<std::mutex> lk(runtime.mtx);
    RecordShutdownDiagnostics(runtime, snapshot);
    if (timed_out || close_failed || called_from_reactor) {
      runtime.accepting = false;
      runtime.quarantined = true;
    } else {
      runtime.lanes.clear();
      runtime.configured_count = 0;
      runtime.quarantined = false;
    }
  }
#ifdef LIBUVBRG_TESTING
  MaybePauseShutdown(testing::ShutdownPausePoint::AfterPublish);
#endif

  if (called_from_reactor)
    return SharedLoopShutdownStatus::CalledFromReactorThread;
  if (timed_out)
    return SharedLoopShutdownStatus::TimedOut;
  if (close_failed)
    return SharedLoopShutdownStatus::LoopCloseFailed;
  return SharedLoopShutdownStatus::Stopped;
}

#ifdef LIBUVBRG_TESTING
namespace testing {

void FailNextLoopCloseCalls(uint32_t lane_index, uint32_t count,
                            int error_code) {
  g_loop_close_fault_error.store(error_code, std::memory_order_release);
  g_loop_close_fault_lane.store(lane_index, std::memory_order_release);
  g_loop_close_fault_remaining.store(count, std::memory_order_release);
}

void ClearLoopCloseFault() {
  g_loop_close_fault_remaining.store(0, std::memory_order_release);
  g_loop_close_fault_lane.store(kNoFaultLane, std::memory_order_release);
}

void ArmShutdownPauses(bool before_close, bool after_publish) {
  ShutdownPauseGate &gate = GetShutdownPauseGate();
  std::lock_guard<std::mutex> lk(gate.mtx);
  gate.before_close = {before_close, false, false};
  gate.after_publish = {after_publish, false, false};
}

bool WaitForShutdownPause(ShutdownPausePoint point,
                          std::chrono::milliseconds timeout) {
  ShutdownPauseGate &gate = GetShutdownPauseGate();
  std::unique_lock<std::mutex> lk(gate.mtx);
  ShutdownPauseState &state = SelectShutdownPause(gate, point);
  return gate.cv.wait_for(lk, timeout, [&]() { return state.waiting; });
}

void ResumeShutdownPause(ShutdownPausePoint point) {
  ShutdownPauseGate &gate = GetShutdownPauseGate();
  std::lock_guard<std::mutex> lk(gate.mtx);
  ShutdownPauseState &state = SelectShutdownPause(gate, point);
  state.released = true;
  gate.cv.notify_all();
}

uint64_t ShutdownRequestCaptureCount() {
  return g_shutdown_request_captures.load(std::memory_order_acquire);
}

} // namespace testing
#endif

} // namespace uvbrg::internal
