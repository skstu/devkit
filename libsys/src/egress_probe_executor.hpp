#pragma once
#include <libstl/executor.hpp>
namespace libsys::detail {
class EgressProbeExecutor final {
public:
    template <class F>
    auto Submit(F&& fn) {
        return executor_.Submit(std::forward<F>(fn));
    }
    void WaitForIdle() {
        executor_.WaitForIdle();
    }
    bool WaitForIdleFor(std::chrono::milliseconds timeout) {
        return executor_.WaitForIdleFor(timeout);
    }

private:
    stl::BoundedExecutor executor_{4, 64, stl::BoundedExecutor::Shutdown::CancelPending};
};
inline EgressProbeExecutor& GetEgressProbeExecutor() {
    static EgressProbeExecutor executor;
    return executor;
}
void WaitForEgressProbeIdle();
}
