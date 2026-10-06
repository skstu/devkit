#pragma once

#include <uv.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace uvbrg::internal {

struct SharedLoopPoolSnapshot {
  uint32_t reactor_count = 0;
  uint64_t active_leases = 0;
  uint64_t queued_tasks = 0;
  uint64_t queued_bytes = 0;
  uint64_t rejected_tasks = 0;
  uint64_t post_failures = 0;
  uint64_t loop_close_failures = 0;
  uint64_t close_retry_attempts = 0;
  uint64_t close_retry_successes = 0;
  uint32_t retained_lanes = 0;
  int final_loop_close_rc = 0;
  bool accepting = false;
  bool quarantined = false;
};

enum class SharedLoopShutdownStatus {
  Stopped,
  AlreadyStopped,
  Busy,
  TimedOut,
  CalledFromReactorThread,
  LoopCloseFailed,
};

class SharedLoopLease {
public:
  ~SharedLoopLease();
  SharedLoopLease(const SharedLoopLease &) = delete;
  SharedLoopLease &operator=(const SharedLoopLease &) = delete;

  bool Post(size_t estimated_bytes, std::function<void(uv_loop_t *)> task,
            std::function<void()> on_cancel = {});
  bool IsLoopThread() const;
  uint32_t reactor_index() const;
  const std::string &routing_key() const;
  void Release();

private:
  friend std::shared_ptr<SharedLoopLease>
  AcquireSharedLoop(const std::string &, size_t, std::string *);
  struct Impl;
  explicit SharedLoopLease(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

std::shared_ptr<SharedLoopLease>
AcquireSharedLoop(const std::string &routing_key, size_t reactor_count,
                  std::string *error);
SharedLoopPoolSnapshot SharedLoopDiagnostics();
SharedLoopShutdownStatus
ShutdownSharedLoops(std::chrono::milliseconds timeout);

#ifdef LIBUVBRG_TESTING
namespace testing {

enum class ShutdownPausePoint { BeforeClose, AfterPublish };

// Process-local fault seam used by the reactor shutdown behavior tests. It is
// intentionally kept out of the public libuvbrg C ABI.
void FailNextLoopCloseCalls(uint32_t lane_index, uint32_t count,
                            int error_code);
void ClearLoopCloseFault();
void ArmShutdownPauses(bool before_close, bool after_publish);
bool WaitForShutdownPause(ShutdownPausePoint point,
                          std::chrono::milliseconds timeout);
void ResumeShutdownPause(ShutdownPausePoint point);
uint64_t ShutdownRequestCaptureCount();

} // namespace testing
#endif

} // namespace uvbrg::internal
