#if !defined(__68AFF022_F299_4312_A6B8_AD8CA8ADA2CE__)
#define __68AFF022_F299_4312_A6B8_AD8CA8ADA2CE__

#include <libstl/include/libstl.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct JSRuntime;
struct JSContext;

namespace engjs {

class IQuickJS final {
public:
  struct Limits {
    size_t memory_limit_bytes = 16 * 1024 * 1024;
    size_t max_stack_bytes = 512 * 1024;
    uint32_t eval_timeout_ms = 250;
  };

  struct CallResult {
    bool ok = false;
    std::string output_json;
    std::string error;
  };

  using ConsoleCallback = std::function<void(const std::string &message)>;

public:
  IQuickJS();
  ~IQuickJS();

public:
  bool Start();
  void Stop();
  bool Ready() const;
  void SetLimits(const Limits &limits);
  Limits GetLimits() const;
  void SetConsoleCallback(ConsoleCallback callback);
  void InterruptEvaluation();

public:
  bool LoadScript(const std::string &script_text,
                  const std::string &source_name = "<memory>",
                  const std::string &entry_name = "handlePolicy",
                  std::string *err_out = nullptr);
  bool LoadScriptFile(const stl::path &script_path,
                      const std::string &entry_name = "handlePolicy",
                      std::string *err_out = nullptr);
  bool AddBootstrapScript(const std::string &script_text,
                          const std::string &source_name = "<memory>",
                          std::string *err_out = nullptr);
  bool Reload(std::string *err_out = nullptr);
  CallResult EvaluateJson(const std::string &input_json);
  CallResult EvaluateJson(const std::string &input_json,
                          uint32_t eval_timeout_ms);
  CallResult EvaluateJson(
      const std::string &input_json, uint32_t eval_timeout_ms,
      std::chrono::steady_clock::time_point absolute_deadline,
      const std::atomic_bool *cancel_flag);
  CallResult EvaluateJson(const std::string &script,
                          const std::string &input_json);

public:
  void SetDefaultScriptPath(const stl::path &script_path);
  stl::path GetDefaultScriptPath() const;
  void SetDefaultScriptText(const std::string &script_text);
  bool EnsureDefaultScriptFile(std::string *err_out = nullptr) const;
  bool EnsureDefaultScriptFile(const stl::path &script_path,
                               std::string *err_out = nullptr) const;
  bool LoadDefaultScript(std::string *err_out = nullptr);
  std::string GetDiagnosticsJson() const;
  void EmitConsoleMessage(const std::string &message) const;

private:
  struct BootstrapScript {
    std::string script_text;
    std::string source_name;
  };

private:
  bool PostTask(const std::function<void()> &task);
  void WorkerLoop(const std::shared_ptr<std::promise<bool>> &started);
  bool CreateRuntimeOnWorker(std::string &err);
  void DestroyContextOnWorker();
  void DestroyRuntimeOnWorker();
  bool ResetContextOnWorker(std::string &err);
  bool InstallBuiltinsOnWorker(std::string &err);
  bool ExecuteScriptOnWorker(const std::string &script_text,
                             const std::string &source_name, std::string &err);
  bool ExecuteBootstrapScriptsOnWorker(std::string &err);
  bool AddBootstrapScriptOnWorker(const std::string &script_text,
                                  const std::string &source_name,
                                  std::string &err);
  bool LoadScriptOnWorker(const std::string &script_text,
                          const std::string &source_name,
                          const std::string &entry_name,
                          const stl::path &script_path, bool from_file,
                          std::string &err);
  CallResult EvaluateJsonOnWorker(const std::string &input_json);
  CallResult EvaluateJsonOnWorker(const std::string &input_json,
                                  uint32_t eval_timeout_ms);
  CallResult EvaluateJsonOnWorker(
      const std::string &input_json, uint32_t eval_timeout_ms,
      std::chrono::steady_clock::time_point absolute_deadline,
      const std::atomic_bool *cancel_flag);
  CallResult EvaluateJsonOnWorker(const std::string &script_text,
                                  const std::string &input_json);
  CallResult EvaluateJsonWithEntryOnWorker(const std::string &input_json,
                                           const std::string &source_name,
                                           const std::string &entry_name,
                                           uint32_t eval_timeout_ms = 0,
                                           std::chrono::steady_clock::time_point
                                               absolute_deadline = {},
                                           const std::atomic_bool
                                               *cancel_flag = nullptr);
  std::string TakeExceptionStringOnWorker();
  void SetLastError(const std::string &err);
  static int InterruptHandler(JSRuntime *rt, void *opaque);

private:
  mutable std::mutex mtx_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::thread worker_;
  std::atomic_bool started_{false};
  std::atomic_bool stop_requested_{false};
  std::atomic_bool interrupt_requested_{false};
  std::atomic_bool loaded_{false};
  Limits limits_;
  ConsoleCallback console_callback_;
  JSRuntime *rt_ = nullptr;
  JSContext *ctx_ = nullptr;
  std::atomic<uint64_t> deadline_ms_{0};
  std::atomic<const std::atomic_bool *> eval_cancel_flag_{nullptr};
  std::string last_error_;
  std::string script_text_;
  std::string source_name_ = "<memory>";
  std::string entry_name_ = "handlePolicy";
  stl::path script_path_;
  bool script_from_file_ = false;
  stl::path default_script_path_;
  std::string default_script_text_;
  std::vector<BootstrapScript> bootstrap_scripts_;
};

class PolicyEnginePool final {
public:
  struct Options {
    std::size_t workers = 4;
    std::size_t max_queue = 1024;
    IQuickJS::Limits limits = {16 * 1024 * 1024, 512 * 1024, 50};
    uint32_t wait_timeout_ms = 75;
    bool fail_open = true;
    std::string source_name = "brosdk://policy/env-info.js";
    std::string entry_name = "handlePolicy";
  };

  struct DecisionRequest {
    std::string input_json = "{}";
    std::string fallback_json = "{}";
    std::string trace_id;
    uint32_t wait_timeout_ms = 0;
  };

  struct DecisionResult {
    bool ok = false;
    bool used_fallback = false;
    bool timed_out = false;
    bool rejected = false;
    uint32_t worker_id = 0;
    uint64_t elapsed_ms = 0;
    std::string output_json;
    std::string error;
  };

  using ConsoleCallback = IQuickJS::ConsoleCallback;
  using Completion = std::function<void(DecisionResult result)>;

public:
  PolicyEnginePool();
  ~PolicyEnginePool();

  PolicyEnginePool(const PolicyEnginePool &) = delete;
  PolicyEnginePool &operator=(const PolicyEnginePool &) = delete;

  bool Start(std::string *err_out = nullptr);
  bool Start(const Options &options, std::string *err_out = nullptr);
  bool StartScript(const Options &options, const std::string &script_text,
                   std::string *err_out = nullptr);
  bool StartScriptFile(const Options &options, const stl::path &script_path,
                       std::string *err_out = nullptr);
  void Stop();
  bool Ready() const;
  DecisionResult Decide(const DecisionRequest &request);
  bool DecideAsync(DecisionRequest request, Completion completion);
  std::string DiagnosticsJson() const;
  void SetConsoleCallback(ConsoleCallback callback);

  static const char *DefaultEnvInfoPolicyScript();

private:
  struct Task;
  struct Worker;

private:
  void WorkerMain(const std::shared_ptr<Worker> &worker);
  void DeadlineMain();
  DecisionResult FallbackResult(const DecisionRequest &request,
                                std::string error, bool rejected,
                                bool timed_out, uint64_t elapsed_ms) const;
  DecisionResult ErrorResult(const DecisionRequest &request, std::string error,
                             bool rejected, bool timed_out,
                             uint64_t elapsed_ms) const;
  uint32_t WaitTimeoutMs(const DecisionRequest &request) const;
  bool TryComplete(const std::shared_ptr<Task> &task, DecisionResult result);
  static Options NormalizeOptions(Options options);

private:
  mutable std::mutex mtx_;
  std::condition_variable cv_;
  Options options_;
  std::string script_text_;
  std::vector<std::shared_ptr<Worker>> workers_;
  std::deque<std::shared_ptr<Task>> queue_;
  std::vector<std::shared_ptr<Task>> inflight_;
  ConsoleCallback console_callback_;
  std::condition_variable deadline_cv_;
  std::thread deadline_thread_;
  bool running_ = false;
  bool stopping_ = false;
  uint64_t next_task_id_ = 1;
  std::size_t active_ = 0;
  std::atomic<uint64_t> submitted_{0};
  std::atomic<uint64_t> accepted_{0};
  std::atomic<uint64_t> rejected_{0};
  std::atomic<uint64_t> completed_{0};
  std::atomic<uint64_t> failed_{0};
  std::atomic<uint64_t> timed_out_{0};
  std::atomic<uint64_t> fallback_{0};
};

} // namespace engjs

/// /*_ Memade®（新生™） _**/
/// /*_ Thu, 16 Apr 2026 00:22:24 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__68AFF022_F299_4312_A6B8_AD8CA8ADA2CE__
