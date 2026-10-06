#include "shared_reactor.h"
#include <future>
#include <iostream>
#include <stdexcept>

using namespace uvbrg::internal;
using namespace std::chrono_literals;

static void Check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

int main() {
  try {
    auto lease = AcquireSharedLoop("shutdown-test", 1, nullptr);
    Check(bool(lease), "acquire");
    Check(ShutdownSharedLoops(100ms) == SharedLoopShutdownStatus::Busy,
          "must retain an active lease");
    const auto lane = lease->reactor_index();
    lease.reset();
    testing::FailNextLoopCloseCalls(lane, 2, UV_EBUSY);
    Check(ShutdownSharedLoops(2s) == SharedLoopShutdownStatus::LoopCloseFailed,
          "close failure must be visible");
    Check(SharedLoopDiagnostics().quarantined, "failed pool must quarantine");
    Check(!AcquireSharedLoop("refused", 1, nullptr), "quarantine refused lease");
    testing::ClearLoopCloseFault();
    Check(ShutdownSharedLoops(2s) == SharedLoopShutdownStatus::Stopped,
          "close retry must recover");
    Check(SharedLoopDiagnostics().close_retry_successes == 1, "retry diagnostic");
    lease = AcquireSharedLoop("next-generation", 1, nullptr);
    Check(bool(lease), "new generation");
    lease.reset();
    testing::ArmShutdownPauses(true, false);
    auto shutdown = std::async(std::launch::async, [] { return ShutdownSharedLoops(2s); });
    const bool paused = testing::WaitForShutdownPause(testing::ShutdownPausePoint::BeforeClose, 2s);
    // Always release the seam before assertions or future destruction.
    const auto competing = paused ? ShutdownSharedLoops(10ms) : SharedLoopShutdownStatus::Stopped;
    testing::ResumeShutdownPause(testing::ShutdownPausePoint::BeforeClose);
    const auto result = shutdown.get();
    Check(paused, "shutdown pause");
    Check(competing == SharedLoopShutdownStatus::TimedOut, "concurrent shutdown deadline");
    Check(result == SharedLoopShutdownStatus::Stopped, "paused shutdown completion");
    Check(ShutdownSharedLoops(100ms) == SharedLoopShutdownStatus::AlreadyStopped,
          "idempotent shutdown");
    std::cout << "Reactor shutdown: lease, quarantine, retry, deadline PASS\n";
    return 0;
  } catch (const std::exception &error) {
    testing::ClearLoopCloseFault();
    testing::ResumeShutdownPause(testing::ShutdownPausePoint::BeforeClose);
    ShutdownSharedLoops(2s);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
