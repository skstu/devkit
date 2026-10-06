#include <libengjs.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <quickjs.h>

#include <algorithm>
#include <limits>
using namespace engjs;

namespace {
constexpr const char kDefaultEntryName[] = "handlePolicy";

constexpr const char kDefaultPolicyScript[] = R"js(
/*
 * BroSDK envInfo policy.
 *
 * Input: one envInfo JSON object.
 * Output: the final envInfo JSON object to use before browser startup.
 *
 * The default policy preserves all unknown envInfo fields and only normalizes
 * proxy/forward/bridgeProxy/proxyUrl. It mirrors the v1 proxy decision rules:
 * - forward wins and is never cleared by global.
 * - forward + proxy => forward is the jump, proxy is the final upstream.
 * - forward without proxy => forward becomes the final upstream.
 * - bridge overrides bridgeProxy for this launch.
 * - proxy without jump => direct proxy.
 * - proxy + bridgeProxy + global=true => skip bridgeProxy.
 * - no proxy and no forward => do not enable the managed proxy chain.
 */
globalThis.handlePolicy = function (input) {
    const isObject = (v) => v && typeof v === "object" && !Array.isArray(v);
    const hasText = (s) => typeof s === "string" && s.trim() !== "";
    const text = (v) => typeof v === "string" ? v.trim() : "";

    const normalizeBoolean = (v) => {
        if (typeof v === "boolean")
            return v;
        if (typeof v === "number")
            return v !== 0;
        if (typeof v === "string") {
            const text = v.trim().toLowerCase();
            return text === "1" || text === "true" || text === "yes" || text === "on";
        }
        return false;
    };

    const result = isObject(input) ? { ...input } : {};
    const originalProxy = text(result.proxy) || text(result.proxyUrl);
    const forward = text(result.forward);
    const explicitBridge = text(result.bridge);
    const boundBridge = explicitBridge || text(result.bridgeProxy);
    const global = normalizeBoolean(result.global);

    if (hasText(forward)) {
        result.forward = forward;
        result.bridge = "";
        if (hasText(originalProxy)) {
            result.proxy = originalProxy;
            result.proxyUrl = originalProxy;
            result.bridgeProxy = forward;
        } else {
            result.proxy = forward;
            result.proxyUrl = forward;
            result.bridgeProxy = "";
        }
        return result;
    }

    result.forward = "";
    if (!hasText(originalProxy)) {
        result.proxy = "";
        result.proxyUrl = "";
        result.bridge = "";
        result.bridgeProxy = "";
        return result;
    }

    result.proxy = originalProxy;
    result.proxyUrl = originalProxy;
    result.bridge = "";
    result.bridgeProxy = global ? "" : boundBridge;
    return result;
};
)js";

constexpr const char kLegacyDefaultPolicyScript[] = R"js(
function normalizeArray(value) {
  return Array.isArray(value) ? value.slice() : [];
}

function firstNonEmptyString() {
  for (let i = 0; i < arguments.length; ++i) {
    const value = arguments[i];
    if (typeof value === "string" && value.length > 0) return value;
  }
  return "";
}

globalThis.handlePolicy = function (input) {
  input = input && typeof input === "object" ? input : {};
  const jumps = normalizeArray(input.jumps);
  const proxyUrl = firstNonEmptyString(input.proxyNormalized, input.proxy);
  const jumpProxy = firstNonEmptyString(input.forward, input.bridge);
  if (!jumps.length && jumpProxy) jumps.push(jumpProxy);

  return {
    mode: proxyUrl || jumps.length ? "passthrough" : "direct",
    proxyUrl: proxyUrl,
    jumps: input.global ? [] : jumps,
    extraArgs: normalizeArray(input.extraArgs),
    reason: jumpProxy
      ? "default policy with jump bridge"
      : "default policy direct/legacy passthrough",
  };
};
)js";

std::string NormalizeScriptText(const std::string &text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const char ch : text) {
    if (ch != '\r')
      normalized.push_back(ch);
  }
  return normalized;
}

bool IsGeneratedLegacyDefaultPolicyScript(const std::string &script_text) {
  if (script_text.empty())
    return false;
  return NormalizeScriptText(script_text) ==
         NormalizeScriptText(kLegacyDefaultPolicyScript);
}

std::string NormalizeSourceName(const std::string &source_name) {
  return source_name.empty() ? std::string("<memory>") : source_name;
}

std::string NormalizeEntryName(const std::string &entry_name) {
  return entry_name.empty() ? std::string(kDefaultEntryName) : entry_name;
}

std::string ValueToString(JSContext *ctx, JSValueConst value) {
  const char *text = JS_ToCString(ctx, value);
  if (!text)
    return {};
  std::string result(text);
  JS_FreeCString(ctx, text);
  return result;
}

uint64_t SteadyMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

uint64_t ElapsedMs(std::chrono::steady_clock::time_point started) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
}

IQuickJS *GetEngine(JSContext *ctx) {
  return static_cast<IQuickJS *>(JS_GetContextOpaque(ctx));
}

JSValue ConsoleWrite(JSContext *ctx, JSValueConst this_val, int argc,
                     JSValueConst *argv) {
  (void)this_val;
  std::string message;
  for (int i = 0; i < argc; ++i) {
    if (!message.empty())
      message.append(" ");
    const std::string chunk = ValueToString(ctx, argv[i]);
    message.append(chunk.empty() ? "[unprintable]" : chunk);
  }
  if (message.empty())
    message = "[empty]";
  if (auto *engine = GetEngine(ctx))
    engine->EmitConsoleMessage(message);
  return JS_UNDEFINED;
}
} // namespace

IQuickJS::IQuickJS() : default_script_text_(kDefaultPolicyScript) {
}

IQuickJS::~IQuickJS() {
  Stop();
}

bool IQuickJS::Start() {
  if (started_.load())
    return true;

  Stop();
  loaded_.store(false);
  stop_requested_.store(false);
  interrupt_requested_.store(false);
  SetLastError("");

  auto started = std::make_shared<std::promise<bool>>();
  auto future = started->get_future();
  worker_ = std::thread([this, started]() { WorkerLoop(started); });

  const bool ok = future.get();
  if (!ok) {
    stop_requested_.store(true);
    cv_.notify_all();
    if (worker_.joinable())
      worker_.join();
  }
  return ok;
}

void IQuickJS::Stop() {
  stop_requested_.store(true);
  cv_.notify_all();

  if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
    worker_.join();

  {
    std::lock_guard<std::mutex> lk(mtx_);
    tasks_.clear();
  }

  started_.store(false);
  loaded_.store(false);
}

bool IQuickJS::Ready() const {
  return started_.load() && loaded_.load();
}

void IQuickJS::SetLimits(const Limits &limits) {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    limits_ = limits;
  }

  if (!started_.load())
    return;

  auto done = std::make_shared<std::promise<bool>>();
  auto future = done->get_future();
  if (!PostTask([this, done, limits]() {
        {
          std::lock_guard<std::mutex> lk(mtx_);
          limits_ = limits;
        }
        if (rt_) {
          JS_SetMemoryLimit(rt_, limits.memory_limit_bytes);
          JS_SetMaxStackSize(rt_, limits.max_stack_bytes);
        }
        done->set_value(true);
      })) {
    return;
  }
  future.get();
}

IQuickJS::Limits IQuickJS::GetLimits() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return limits_;
}

void IQuickJS::SetConsoleCallback(ConsoleCallback callback) {
  std::lock_guard<std::mutex> lk(mtx_);
  console_callback_ = std::move(callback);
}

void IQuickJS::InterruptEvaluation() {
  interrupt_requested_.store(true);
}

bool IQuickJS::LoadScript(const std::string &script_text,
                          const std::string &source_name,
                          const std::string &entry_name, std::string *err_out) {
  if (!started_.load()) {
    if (err_out)
      *err_out = "IQuickJS is not started.";
    return false;
  }
  if (script_text.empty()) {
    if (err_out)
      *err_out = "Policy script text is empty.";
    return false;
  }

  auto done = std::make_shared<std::promise<bool>>();
  auto future = done->get_future();
  auto error = std::make_shared<std::string>();
  if (!PostTask([this, done, error, script_text, source_name, entry_name]() {
        done->set_value(LoadScriptOnWorker(script_text, source_name, entry_name,
                                           stl::path(), false, *error));
      })) {
    if (err_out)
      *err_out = "Failed to enqueue policy script load task.";
    return false;
  }

  const bool ok = future.get();
  if (err_out)
    *err_out = *error;
  return ok;
}

bool IQuickJS::LoadScriptFile(const stl::path &script_path,
                              const std::string &entry_name,
                              std::string *err_out) {
  if (!started_.load()) {
    if (err_out)
      *err_out = "IQuickJS is not started.";
    return false;
  }

  const stl::path normalized = script_path.lexically_normal();
  if (normalized.empty()) {
    if (err_out)
      *err_out = "Policy script path is empty.";
    return false;
  }
  if (!stl::Utils::FileExists(normalized)) {
    if (err_out)
      *err_out = fmt::format("Policy script file does not exist: {}",
                             normalized.string());
    return false;
  }

  const std::string script_text = stl::Utils::ReadFile(normalized);
  if (script_text.empty()) {
    if (err_out)
      *err_out =
          fmt::format("Policy script file is empty: {}", normalized.string());
    return false;
  }

  auto done = std::make_shared<std::promise<bool>>();
  auto future = done->get_future();
  auto error = std::make_shared<std::string>();
  const std::string source_name = normalized.string();
  const std::string target_entry_name = NormalizeEntryName(entry_name);
  if (!PostTask([this, done, error, script_text, source_name, target_entry_name,
                 normalized]() {
        done->set_value(LoadScriptOnWorker(script_text, source_name,
                                           target_entry_name, normalized, true,
                                           *error));
      })) {
    if (err_out)
      *err_out = "Failed to enqueue policy script file load task.";
    return false;
  }

  const bool ok = future.get();
  if (err_out)
    *err_out = *error;
  return ok;
}

bool IQuickJS::AddBootstrapScript(const std::string &script_text,
                                  const std::string &source_name,
                                  std::string *err_out) {
  if (!started_.load()) {
    if (err_out)
      *err_out = "IQuickJS is not started.";
    return false;
  }
  if (script_text.empty()) {
    if (err_out)
      *err_out = "Bootstrap script text is empty.";
    return false;
  }

  auto done = std::make_shared<std::promise<bool>>();
  auto future = done->get_future();
  auto error = std::make_shared<std::string>();
  if (!PostTask([this, done, error, script_text, source_name]() {
        done->set_value(
            AddBootstrapScriptOnWorker(script_text, source_name, *error));
      })) {
    if (err_out)
      *err_out = "Failed to enqueue bootstrap script load task.";
    return false;
  }

  const bool ok = future.get();
  if (err_out)
    *err_out = *error;
  return ok;
}

bool IQuickJS::Reload(std::string *err_out) {
  stl::path script_path;
  bool script_from_file = false;
  std::string script_text;
  std::string source_name;
  std::string entry_name;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    script_path = script_path_;
    script_from_file = script_from_file_;
    script_text = script_text_;
    source_name = source_name_;
    entry_name = entry_name_;
  }

  if (script_from_file && !script_path.empty())
    return LoadScriptFile(script_path, entry_name, err_out);

  if (script_text.empty()) {
    if (err_out)
      *err_out = "No policy script has been loaded yet.";
    return false;
  }
  return LoadScript(script_text, source_name, entry_name, err_out);
}

IQuickJS::CallResult IQuickJS::EvaluateJson(const std::string &input_json) {
  CallResult result;
  if (!started_.load()) {
    result.error = "IQuickJS is not started.";
    return result;
  }
  if (!loaded_.load()) {
    result.error = "Policy script is not loaded.";
    return result;
  }

  auto done = std::make_shared<std::promise<CallResult>>();
  auto future = done->get_future();
  const std::string payload =
      input_json.empty() ? std::string("{}") : input_json;
  if (!PostTask([this, done, payload]() {
        done->set_value(EvaluateJsonOnWorker(payload));
      })) {
    result.error = "Failed to enqueue policy evaluation task.";
    return result;
  }
  return future.get();
}

IQuickJS::CallResult IQuickJS::EvaluateJson(const std::string &input_json,
                                            uint32_t eval_timeout_ms) {
  return EvaluateJson(input_json, eval_timeout_ms,
                      std::chrono::steady_clock::time_point{}, nullptr);
}

IQuickJS::CallResult IQuickJS::EvaluateJson(
    const std::string &input_json, uint32_t eval_timeout_ms,
    std::chrono::steady_clock::time_point absolute_deadline,
    const std::atomic_bool *cancel_flag) {
  CallResult result;
  if (!started_.load()) {
    result.error = "IQuickJS is not started.";
    return result;
  }
  if (!loaded_.load()) {
    result.error = "Policy script is not loaded.";
    return result;
  }

  auto done = std::make_shared<std::promise<CallResult>>();
  auto future = done->get_future();
  const std::string payload =
      input_json.empty() ? std::string("{}") : input_json;
  if (!PostTask([this, done, payload, eval_timeout_ms, absolute_deadline,
                 cancel_flag]() {
        done->set_value(EvaluateJsonOnWorker(
            payload, eval_timeout_ms, absolute_deadline, cancel_flag));
      })) {
    result.error = "Failed to enqueue policy evaluation task.";
    return result;
  }
  return future.get();
}

IQuickJS::CallResult IQuickJS::EvaluateJson(const std::string &script,
                                            const std::string &input_json) {
  CallResult result;
  if (!started_.load()) {
    result.error = "IQuickJS is not started.";
    return result;
  }
  if (script.empty()) {
    result.error = "Policy script text is empty.";
    return result;
  }

  auto done = std::make_shared<std::promise<CallResult>>();
  auto future = done->get_future();
  const std::string payload =
      input_json.empty() ? std::string("{}") : input_json;
  if (!PostTask([this, done, script, payload]() {
        done->set_value(EvaluateJsonOnWorker(script, payload));
      })) {
    result.error = "Failed to enqueue dynamic policy evaluation task.";
    return result;
  }
  return future.get();
}

void IQuickJS::SetDefaultScriptPath(const stl::path &script_path) {
  std::lock_guard<std::mutex> lk(mtx_);
  default_script_path_ = script_path.lexically_normal();
}

stl::path IQuickJS::GetDefaultScriptPath() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return default_script_path_;
}

void IQuickJS::SetDefaultScriptText(const std::string &script_text) {
  std::lock_guard<std::mutex> lk(mtx_);
  default_script_text_ =
      script_text.empty() ? std::string(kDefaultPolicyScript) : script_text;
}

bool IQuickJS::EnsureDefaultScriptFile(std::string *err_out) const {
  return EnsureDefaultScriptFile(GetDefaultScriptPath(), err_out);
}

bool IQuickJS::EnsureDefaultScriptFile(const stl::path &script_path,
                                       std::string *err_out) const {
  if (script_path.empty()) {
    if (err_out)
      *err_out = "Default policy script path is empty.";
    return false;
  }

  std::string default_script_text;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    default_script_text = default_script_text_;
  }
  if (default_script_text.empty()) {
    if (err_out)
      *err_out = "Default policy script text is empty.";
    return false;
  }

  if (stl::Utils::FileExists(script_path)) {
    const std::string existing_script = stl::Utils::ReadFile(script_path);
    if (!existing_script.empty() &&
        !IsGeneratedLegacyDefaultPolicyScript(existing_script)) {
      return true;
    }
  }

  if (!stl::Utils::MakeDirectory(script_path.parent_path())) {
    if (err_out)
      *err_out = fmt::format("Failed to create policy script directory: {}",
                             script_path.parent_path().string());
    return false;
  }

  if (!stl::Utils::WriteFile(script_path, default_script_text)) {
    if (err_out)
      *err_out = fmt::format("Failed to write default policy script: {}",
                             script_path.string());
    return false;
  }
  return true;
}

bool IQuickJS::LoadDefaultScript(std::string *err_out) {
  const stl::path script_path = GetDefaultScriptPath();
  if (!EnsureDefaultScriptFile(script_path, err_out))
    return false;
  return LoadScriptFile(script_path, kDefaultEntryName, err_out);
}

std::string IQuickJS::GetDiagnosticsJson() const {
  nlohmann::json doc = {
      {"started", started_.load()},
      {"loaded", loaded_.load()},
  };

  Limits limits;
  std::string last_error;
  std::string source_name;
  std::string entry_name;
  stl::path script_path;
  stl::path default_script_path;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    limits = limits_;
    last_error = last_error_;
    source_name = source_name_;
    entry_name = entry_name_;
    script_path = script_path_;
    default_script_path = default_script_path_;
  }

  doc["sourceName"] = source_name;
  doc["entryName"] = entry_name;
  doc["lastError"] = last_error;
  doc["defaultScriptPath"] = default_script_path.string();
  doc["scriptPath"] = script_path.string();
  size_t bootstrap_script_count = 0;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    bootstrap_script_count = bootstrap_scripts_.size();
  }
  doc["bootstrapScriptCount"] = bootstrap_script_count;
  doc["limits"] = {
      {"memoryLimitBytes", limits.memory_limit_bytes},
      {"maxStackBytes", limits.max_stack_bytes},
      {"evalTimeoutMs", limits.eval_timeout_ms},
  };
  return doc.dump();
}

bool IQuickJS::PostTask(const std::function<void()> &task) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!worker_.joinable() || stop_requested_.load())
    return false;
  tasks_.push_back(task);
  cv_.notify_one();
  return true;
}

void IQuickJS::WorkerLoop(const std::shared_ptr<std::promise<bool>> &started) {
  std::string err;
  const bool ready = CreateRuntimeOnWorker(err);
  SetLastError(err);
  started_.store(ready);
  loaded_.store(false);
  started->set_value(ready);
  if (!ready) {
    DestroyRuntimeOnWorker();
    return;
  }

  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait(lk,
               [this]() { return stop_requested_.load() || !tasks_.empty(); });
      if (stop_requested_.load() && tasks_.empty())
        break;
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    if (task)
      task();
  }

  DestroyRuntimeOnWorker();
  started_.store(false);
  loaded_.store(false);
}

bool IQuickJS::CreateRuntimeOnWorker(std::string &err) {
  rt_ = JS_NewRuntime();
  if (!rt_) {
    err = "JS_NewRuntime failed.";
    return false;
  }

  const Limits limits = GetLimits();
  JS_SetRuntimeOpaque(rt_, this);
  JS_SetMemoryLimit(rt_, limits.memory_limit_bytes);
  JS_SetMaxStackSize(rt_, limits.max_stack_bytes);
  JS_SetInterruptHandler(rt_, &IQuickJS::InterruptHandler, this);
  JS_UpdateStackTop(rt_);

  if (!ResetContextOnWorker(err)) {
    DestroyRuntimeOnWorker();
    return false;
  }
  return true;
}

void IQuickJS::DestroyContextOnWorker() {
  if (!ctx_)
    return;
  JS_FreeContext(ctx_);
  ctx_ = nullptr;
}

void IQuickJS::DestroyRuntimeOnWorker() {
  DestroyContextOnWorker();
  if (!rt_)
    return;
  JS_FreeRuntime(rt_);
  rt_ = nullptr;
}

bool IQuickJS::ResetContextOnWorker(std::string &err) {
  DestroyContextOnWorker();
  ctx_ = JS_NewContext(rt_);
  if (!ctx_) {
    err = "JS_NewContext failed.";
    return false;
  }
  JS_SetContextOpaque(ctx_, this);
  if (!InstallBuiltinsOnWorker(err))
    return false;
  return ExecuteBootstrapScriptsOnWorker(err);
}

bool IQuickJS::InstallBuiltinsOnWorker(std::string &err) {
  JSValue global = JS_GetGlobalObject(ctx_);
  if (JS_IsException(global)) {
    err = TakeExceptionStringOnWorker();
    return false;
  }

  JSValue console = JS_NewObject(ctx_);
  if (JS_IsException(console)) {
    err = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, global);
    return false;
  }

  if (JS_SetPropertyStr(ctx_, console, "log",
                        JS_NewCFunction(ctx_, ConsoleWrite, "log", 1)) < 0 ||
      JS_SetPropertyStr(ctx_, console, "warn",
                        JS_NewCFunction(ctx_, ConsoleWrite, "warn", 1)) < 0 ||
      JS_SetPropertyStr(ctx_, console, "error",
                        JS_NewCFunction(ctx_, ConsoleWrite, "error", 1)) < 0) {
    err = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, console);
    JS_FreeValue(ctx_, global);
    return false;
  }

  if (JS_SetPropertyStr(ctx_, global, "console", console) < 0 ||
      JS_SetPropertyStr(ctx_, global, "print",
                        JS_NewCFunction(ctx_, ConsoleWrite, "print", 1)) < 0) {
    err = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, global);
    return false;
  }

  JS_FreeValue(ctx_, global);
  return true;
}

bool IQuickJS::ExecuteScriptOnWorker(const std::string &script_text,
                                     const std::string &source_name,
                                     std::string &err) {
  interrupt_requested_.store(false);
  deadline_ms_.store(SteadyMs() + GetLimits().eval_timeout_ms,
                     std::memory_order_release);
  JSValue eval_result = JS_Eval(ctx_, script_text.data(), script_text.size(),
                                source_name.c_str(), JS_EVAL_TYPE_GLOBAL);
  deadline_ms_.store(0, std::memory_order_release);
  interrupt_requested_.store(false);
  if (JS_IsException(eval_result)) {
    err = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, eval_result);
    return false;
  }
  JS_FreeValue(ctx_, eval_result);
  return true;
}

bool IQuickJS::ExecuteBootstrapScriptsOnWorker(std::string &err) {
  std::vector<BootstrapScript> scripts;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    scripts = bootstrap_scripts_;
  }

  for (const auto &script : scripts) {
    if (!ExecuteScriptOnWorker(script.script_text, script.source_name, err))
      return false;
  }
  return true;
}

bool IQuickJS::AddBootstrapScriptOnWorker(const std::string &script_text,
                                          const std::string &source_name,
                                          std::string &err) {
  if (!ctx_) {
    err = "QuickJS context is not ready.";
    SetLastError(err);
    return false;
  }

  const std::string target_source_name = NormalizeSourceName(source_name);
  if (!ExecuteScriptOnWorker(script_text, target_source_name, err)) {
    SetLastError(err);
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(mtx_);
    auto found =
        std::find_if(bootstrap_scripts_.begin(), bootstrap_scripts_.end(),
                     [&target_source_name](const BootstrapScript &script) {
                       return script.source_name == target_source_name;
                     });
    if (found != bootstrap_scripts_.end()) {
      found->script_text = script_text;
      found->source_name = target_source_name;
    } else {
      bootstrap_scripts_.push_back(
          BootstrapScript{script_text, target_source_name});
    }
    last_error_.clear();
  }
  return true;
}

bool IQuickJS::LoadScriptOnWorker(const std::string &script_text,
                                  const std::string &source_name,
                                  const std::string &entry_name,
                                  const stl::path &script_path, bool from_file,
                                  std::string &err) {
  if (!rt_) {
    err = "QuickJS runtime is not ready.";
    SetLastError(err);
    loaded_.store(false);
    return false;
  }

  const std::string target_source_name = NormalizeSourceName(source_name);
  const std::string target_entry_name = NormalizeEntryName(entry_name);

  if (!ResetContextOnWorker(err)) {
    SetLastError(err);
    loaded_.store(false);
    return false;
  }

  if (!ExecuteScriptOnWorker(script_text, target_source_name, err)) {
    SetLastError(err);
    loaded_.store(false);
    return false;
  }

  JSValue global = JS_GetGlobalObject(ctx_);
  JSValue entry = JS_GetPropertyStr(ctx_, global, target_entry_name.c_str());
  const bool is_function = JS_IsFunction(ctx_, entry);
  JS_FreeValue(ctx_, entry);
  JS_FreeValue(ctx_, global);
  if (!is_function) {
    err = fmt::format("Policy entry '{}' is missing or not a function.",
                      target_entry_name);
    SetLastError(err);
    loaded_.store(false);
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(mtx_);
    script_text_ = script_text;
    source_name_ = target_source_name;
    entry_name_ = target_entry_name;
    script_path_ = script_path;
    script_from_file_ = from_file;
    last_error_.clear();
  }
  loaded_.store(true);
  return true;
}

IQuickJS::CallResult
IQuickJS::EvaluateJsonOnWorker(const std::string &input_json) {
  CallResult result;
  if (!ctx_) {
    result.error = "QuickJS context is not ready.";
    SetLastError(result.error);
    return result;
  }

  std::string source_name;
  std::string entry_name;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    source_name = source_name_;
    entry_name = entry_name_;
  }

  return EvaluateJsonWithEntryOnWorker(input_json, source_name, entry_name);
}

IQuickJS::CallResult
IQuickJS::EvaluateJsonOnWorker(const std::string &input_json,
                               uint32_t eval_timeout_ms) {
  return EvaluateJsonOnWorker(input_json, eval_timeout_ms,
                              std::chrono::steady_clock::time_point{},
                              nullptr);
}

IQuickJS::CallResult IQuickJS::EvaluateJsonOnWorker(
    const std::string &input_json, uint32_t eval_timeout_ms,
    std::chrono::steady_clock::time_point absolute_deadline,
    const std::atomic_bool *cancel_flag) {
  CallResult result;
  if (!ctx_) {
    result.error = "QuickJS context is not ready.";
    SetLastError(result.error);
    return result;
  }

  std::string source_name;
  std::string entry_name;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    source_name = source_name_;
    entry_name = entry_name_;
  }

  return EvaluateJsonWithEntryOnWorker(input_json, source_name, entry_name,
                                       eval_timeout_ms, absolute_deadline,
                                       cancel_flag);
}

IQuickJS::CallResult
IQuickJS::EvaluateJsonOnWorker(const std::string &script_text,
                               const std::string &input_json) {
  CallResult result;
  if (!rt_) {
    result.error = "QuickJS runtime is not ready.";
    SetLastError(result.error);
    return result;
  }
  if (script_text.empty()) {
    result.error = "Policy script text is empty.";
    SetLastError(result.error);
    return result;
  }

  const bool restore_loaded = loaded_.load();
  std::string restore_script_text;
  std::string restore_source_name;
  std::string restore_entry_name;
  stl::path restore_script_path;
  bool restore_script_from_file = false;
  if (restore_loaded) {
    std::lock_guard<std::mutex> lk(mtx_);
    restore_script_text = script_text_;
    restore_source_name = source_name_;
    restore_entry_name = entry_name_;
    restore_script_path = script_path_;
    restore_script_from_file = script_from_file_;
  }

  auto restore_context = [&]() -> std::string {
    std::string restore_error;
    if (restore_loaded) {
      if (!LoadScriptOnWorker(restore_script_text, restore_source_name,
                              restore_entry_name, restore_script_path,
                              restore_script_from_file, restore_error)) {
        return fmt::format("Policy script restore failed: {}", restore_error);
      }
      return {};
    }

    if (!ResetContextOnWorker(restore_error)) {
      return fmt::format("QuickJS context restore failed: {}", restore_error);
    }
    return {};
  };

  auto finish_with_restore = [&](CallResult value) -> CallResult {
    const std::string restore_error = restore_context();
    if (!restore_error.empty()) {
      value.ok = false;
      value.output_json.clear();
      value.error = value.error.empty()
                        ? restore_error
                        : fmt::format("{}; evaluation error: {}", restore_error,
                                      value.error);
      SetLastError(value.error);
      return value;
    }
    if (!value.ok)
      SetLastError(value.error);
    return value;
  };

  if (!ResetContextOnWorker(result.error)) {
    SetLastError(result.error);
    return finish_with_restore(result);
  }

  constexpr const char kDynamicSourceName[] = "<dynamic-policy>";
  if (!ExecuteScriptOnWorker(script_text, kDynamicSourceName, result.error)) {
    SetLastError(result.error);
    return finish_with_restore(result);
  }

  result =
      EvaluateJsonWithEntryOnWorker(input_json, kDynamicSourceName,
                                    kDefaultEntryName);
  return finish_with_restore(result);
}

IQuickJS::CallResult IQuickJS::EvaluateJsonWithEntryOnWorker(
    const std::string &input_json, const std::string &source_name,
    const std::string &entry_name, uint32_t eval_timeout_ms,
    std::chrono::steady_clock::time_point absolute_deadline,
    const std::atomic_bool *cancel_flag) {
  CallResult result;
  if (!ctx_) {
    result.error = "QuickJS context is not ready.";
    SetLastError(result.error);
    return result;
  }
  if (cancel_flag && cancel_flag->load()) {
    result.error = "Policy evaluation was cancelled before start.";
    SetLastError(result.error);
    return result;
  }
  if (absolute_deadline != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= absolute_deadline) {
    result.error = "Policy evaluation deadline expired before start.";
    SetLastError(result.error);
    return result;
  }

  uint32_t timeout_ms = eval_timeout_ms;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (timeout_ms == 0)
      timeout_ms = limits_.eval_timeout_ms;
  }
  if (absolute_deadline != std::chrono::steady_clock::time_point{}) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= absolute_deadline) {
      result.error = "Policy evaluation deadline expired before start.";
      SetLastError(result.error);
      return result;
    }
    const auto remaining_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            absolute_deadline - now)
            .count();
    const uint32_t bounded_remaining_ms =
        remaining_ms <= 0 ? 1 : static_cast<uint32_t>(remaining_ms);
    timeout_ms = timeout_ms == 0
                     ? bounded_remaining_ms
                     : std::min<uint32_t>(timeout_ms, bounded_remaining_ms);
  }

  JSValue input =
      JS_ParseJSON(ctx_, input_json.data(), input_json.size(),
                   source_name.empty() ? "<input>" : source_name.c_str());
  if (JS_IsException(input)) {
    result.error = TakeExceptionStringOnWorker();
    SetLastError(result.error);
    return result;
  }

  JSValue global = JS_GetGlobalObject(ctx_);
  JSValue entry = JS_GetPropertyStr(ctx_, global, entry_name.c_str());
  if (!JS_IsFunction(ctx_, entry)) {
    JS_FreeValue(ctx_, input);
    JS_FreeValue(ctx_, entry);
    JS_FreeValue(ctx_, global);
    result.error = fmt::format(
        "Policy entry '{}' is missing or not a function.", entry_name);
    SetLastError(result.error);
    return result;
  }

  JSValue argv[] = {input};
  if (cancel_flag && cancel_flag->load()) {
    JS_FreeValue(ctx_, input);
    JS_FreeValue(ctx_, entry);
    JS_FreeValue(ctx_, global);
    result.error = "Policy evaluation was cancelled before start.";
    SetLastError(result.error);
    return result;
  }
  if (absolute_deadline != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() >= absolute_deadline) {
    JS_FreeValue(ctx_, input);
    JS_FreeValue(ctx_, entry);
    JS_FreeValue(ctx_, global);
    result.error = "Policy evaluation deadline expired before start.";
    SetLastError(result.error);
    return result;
  }
  eval_cancel_flag_.store(cancel_flag, std::memory_order_release);
  interrupt_requested_.store(false);
  const auto relative_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  const auto call_deadline =
      absolute_deadline == std::chrono::steady_clock::time_point{}
          ? relative_deadline
          : std::min(absolute_deadline, relative_deadline);
  deadline_ms_.store(
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                call_deadline.time_since_epoch())
                                .count()),
      std::memory_order_release);
  JSValue output = JS_Call(ctx_, entry, global, 1, argv);
  deadline_ms_.store(0, std::memory_order_release);
  eval_cancel_flag_.store(nullptr, std::memory_order_release);
  interrupt_requested_.store(false);
  JS_FreeValue(ctx_, input);
  JS_FreeValue(ctx_, entry);
  JS_FreeValue(ctx_, global);
  if (JS_IsException(output)) {
    result.error = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, output);
    SetLastError(result.error);
    return result;
  }

  JSValue json = JS_JSONStringify(ctx_, output, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(ctx_, output);
  if (JS_IsException(json)) {
    result.error = TakeExceptionStringOnWorker();
    JS_FreeValue(ctx_, json);
    SetLastError(result.error);
    return result;
  }

  if (JS_IsUndefined(json))
    result.output_json = "null";
  else
    result.output_json = ValueToString(ctx_, json);
  JS_FreeValue(ctx_, json);

  if (result.output_json.empty())
    result.output_json = "null";
  result.ok = true;
  SetLastError("");
  return result;
}

std::string IQuickJS::TakeExceptionStringOnWorker() {
  if (!ctx_)
    return "QuickJS exception.";

  JSValue exception = JS_GetException(ctx_);
  std::string message = ValueToString(ctx_, exception);
  JSValue stack = JS_GetPropertyStr(ctx_, exception, "stack");
  const std::string stack_text =
      JS_IsException(stack) ? std::string() : ValueToString(ctx_, stack);
  JS_FreeValue(ctx_, stack);
  JS_FreeValue(ctx_, exception);

  if (!stack_text.empty() && stack_text != message) {
    if (!message.empty())
      message.append("\n");
    message.append(stack_text);
  }
  if (message.empty())
    message = "QuickJS exception.";
  return message;
}

void IQuickJS::EmitConsoleMessage(const std::string &message) const {
  ConsoleCallback callback;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    callback = console_callback_;
  }
  if (callback)
    callback(message);
}

void IQuickJS::SetLastError(const std::string &err) {
  std::lock_guard<std::mutex> lk(mtx_);
  last_error_ = err;
}

int IQuickJS::InterruptHandler(JSRuntime *rt, void *opaque) {
  (void)rt;
  auto *engine = static_cast<IQuickJS *>(opaque);
  if (!engine)
    return 0;
  if (engine->stop_requested_.load())
    return 1;
  if (engine->interrupt_requested_.load())
    return 1;
  const auto *cancel_flag =
      engine->eval_cancel_flag_.load(std::memory_order_acquire);
  if (cancel_flag && cancel_flag->load())
    return 1;
  const uint64_t deadline_ms =
      engine->deadline_ms_.load(std::memory_order_acquire);
  if (deadline_ms == 0)
    return 0;
  return SteadyMs() >= deadline_ms ? 1 : 0;
}

struct PolicyEnginePool::Task {
  uint64_t id = 0;
  DecisionRequest request;
  Completion completion;
  std::chrono::steady_clock::time_point started{};
  std::chrono::steady_clock::time_point deadline{};
  uint32_t wait_timeout_ms = 0;
  std::weak_ptr<Worker> worker;
  std::atomic_bool active_counted{false};
  std::atomic_bool finished{false};
};

struct PolicyEnginePool::Worker {
  uint32_t id = 0;
  IQuickJS engine;
  std::thread thread;
  bool ready = false;
  std::string error;
};

PolicyEnginePool::PolicyEnginePool() = default;

PolicyEnginePool::~PolicyEnginePool() {
  Stop();
}

PolicyEnginePool::Options
PolicyEnginePool::NormalizeOptions(PolicyEnginePool::Options options) {
  if (options.workers == 0)
    options.workers = 1;
  options.workers = std::min<std::size_t>(options.workers, 64);
  if (options.max_queue == 0)
    options.max_queue = 1;
  if (options.limits.eval_timeout_ms == 0)
    options.limits.eval_timeout_ms = 50;
  if (options.wait_timeout_ms == 0)
    options.wait_timeout_ms = options.limits.eval_timeout_ms + 25;
  options.source_name = NormalizeSourceName(options.source_name);
  options.entry_name = NormalizeEntryName(options.entry_name);
  return options;
}

bool PolicyEnginePool::Start(std::string *err_out) {
  return StartScript(Options{}, DefaultEnvInfoPolicyScript(), err_out);
}

bool PolicyEnginePool::Start(const Options &options, std::string *err_out) {
  return StartScript(options, DefaultEnvInfoPolicyScript(), err_out);
}

bool PolicyEnginePool::StartScript(const Options &raw_options,
                                   const std::string &script_text,
                                   std::string *err_out) {
  Stop();
  if (script_text.empty()) {
    if (err_out)
      *err_out = "Policy script text is empty.";
    return false;
  }

  Options options = NormalizeOptions(raw_options);
  std::vector<std::shared_ptr<Worker>> workers;
  workers.reserve(options.workers);
  std::string first_error;

  for (std::size_t i = 0; i < options.workers; ++i) {
    auto worker = std::make_shared<Worker>();
    worker->id = static_cast<uint32_t>(i + 1);
    worker->engine.SetLimits(options.limits);
    worker->engine.SetConsoleCallback(console_callback_);
    if (!worker->engine.Start()) {
      worker->error = "QuickJS worker failed to start.";
      if (first_error.empty())
        first_error = worker->error;
      break;
    }
    if (!worker->engine.LoadScript(script_text, options.source_name,
                                   options.entry_name, &worker->error)) {
      if (first_error.empty())
        first_error = worker->error;
      break;
    }
    worker->ready = true;
    workers.push_back(worker);
  }

  if (workers.size() != options.workers) {
    for (auto &worker : workers)
      worker->engine.Stop();
    if (err_out)
      *err_out = first_error.empty() ? "Failed to start policy engine pool."
                                     : first_error;
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(mtx_);
    options_ = options;
    script_text_ = script_text;
    workers_ = std::move(workers);
    queue_.clear();
    inflight_.clear();
    active_ = 0;
    stopping_ = false;
    running_ = true;
    next_task_id_ = 1;
  }

  for (auto &worker : workers_) {
    worker->thread =
        std::thread([this, worker]() { WorkerMain(worker); });
  }
  deadline_thread_ = std::thread([this]() { DeadlineMain(); });

  if (err_out)
    err_out->clear();
  return true;
}

bool PolicyEnginePool::StartScriptFile(const Options &options,
                                       const stl::path &script_path,
                                       std::string *err_out) {
  if (script_path.empty()) {
    if (err_out)
      *err_out = "Policy script path is empty.";
    return false;
  }
  const std::string script_text = stl::Utils::ReadFile(script_path);
  if (script_text.empty()) {
    if (err_out)
      *err_out = "Policy script file is empty or cannot be read: " +
                 script_path.string();
    return false;
  }
  Options resolved = options;
  if (resolved.source_name.empty())
    resolved.source_name = script_path.string();
  return StartScript(resolved, script_text, err_out);
}

void PolicyEnginePool::Stop() {
  std::vector<std::shared_ptr<Worker>> workers;
  std::deque<std::shared_ptr<Task>> rejected_tasks;
  std::vector<std::shared_ptr<Task>> inflight_tasks;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_ && workers_.empty())
      return;
    stopping_ = true;
    running_ = false;
    workers.swap(workers_);
    rejected_tasks.swap(queue_);
    inflight_tasks.swap(inflight_);
  }
  cv_.notify_all();
  deadline_cv_.notify_all();

  for (auto &task : rejected_tasks) {
    if (!task)
      continue;
    auto result = FallbackResult(task->request,
                                 "Policy engine stopped before task ran.",
                                 true, false, 0);
    TryComplete(task, std::move(result));
  }
  for (auto &task : inflight_tasks) {
    if (!task)
      continue;
    auto result = FallbackResult(task->request, "Policy engine stopped.",
                                 true, false, 0);
    TryComplete(task, std::move(result));
  }

  for (auto &worker : workers) {
    if (worker->thread.joinable() &&
        worker->thread.get_id() != std::this_thread::get_id())
      worker->thread.join();
    worker->engine.Stop();
  }
  if (deadline_thread_.joinable() &&
      deadline_thread_.get_id() != std::this_thread::get_id())
    deadline_thread_.join();
}

bool PolicyEnginePool::Ready() const {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!running_ || workers_.empty())
    return false;
  for (const auto &worker : workers_) {
    if (!worker || !worker->ready || !worker->engine.Ready())
      return false;
  }
  return true;
}

PolicyEnginePool::DecisionResult
PolicyEnginePool::FallbackResult(const DecisionRequest &request,
                                 std::string error, bool rejected,
                                 bool timed_out,
                                 uint64_t elapsed_ms) const {
  bool fail_open = true;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    fail_open = options_.fail_open;
  }
  if (!fail_open)
    return ErrorResult(request, std::move(error), rejected, timed_out,
                       elapsed_ms);

  DecisionResult result;
  result.ok = false;
  result.used_fallback = true;
  result.rejected = rejected;
  result.timed_out = timed_out;
  result.elapsed_ms = elapsed_ms;
  result.output_json =
      request.fallback_json.empty() ? std::string("{}") : request.fallback_json;
  result.error = std::move(error);
  return result;
}

PolicyEnginePool::DecisionResult
PolicyEnginePool::ErrorResult(const DecisionRequest &request,
                              std::string error, bool rejected,
                              bool timed_out, uint64_t elapsed_ms) const {
  (void)request;
  DecisionResult result;
  result.ok = false;
  result.used_fallback = false;
  result.rejected = rejected;
  result.timed_out = timed_out;
  result.elapsed_ms = elapsed_ms;
  result.output_json = "{}";
  result.error = std::move(error);
  return result;
}

uint32_t PolicyEnginePool::WaitTimeoutMs(
    const DecisionRequest &request) const {
  if (request.wait_timeout_ms != 0)
    return request.wait_timeout_ms;
  std::lock_guard<std::mutex> lk(mtx_);
  return options_.wait_timeout_ms == 0 ? 75 : options_.wait_timeout_ms;
}

bool PolicyEnginePool::TryComplete(const std::shared_ptr<Task> &task,
                                   DecisionResult result) {
  if (!task || !task->completion)
    return false;
  if (task->finished.exchange(true))
    return false;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    queue_.erase(
        std::remove_if(queue_.begin(), queue_.end(),
                       [&](const std::shared_ptr<Task> &entry) {
                         return !entry || entry.get() == task.get();
                       }),
        queue_.end());
    inflight_.erase(
        std::remove_if(inflight_.begin(), inflight_.end(),
                       [&](const std::shared_ptr<Task> &entry) {
                         return !entry || entry.get() == task.get();
                       }),
        inflight_.end());
    if (task->active_counted.exchange(false) && active_ > 0)
      --active_;
  }
  deadline_cv_.notify_one();
  task->completion(std::move(result));
  return true;
}

PolicyEnginePool::DecisionResult
PolicyEnginePool::Decide(const DecisionRequest &request) {
  const auto started = std::chrono::steady_clock::now();
  auto done = std::make_shared<std::promise<DecisionResult>>();
  auto future = done->get_future();
  DecisionRequest queued_request = request;
  if (!DecideAsync(std::move(queued_request),
                   [done](DecisionResult result) mutable {
                     done->set_value(std::move(result));
                   })) {
    if (future.wait_for(std::chrono::milliseconds(0)) ==
        std::future_status::ready) {
      return future.get();
    }
    return FallbackResult(request, "Policy engine rejected async decision.",
                          true, false, ElapsedMs(started));
  }
  return future.get();
}

bool PolicyEnginePool::DecideAsync(DecisionRequest request,
                                   Completion completion) {
  if (!completion)
    return false;
  submitted_.fetch_add(1, std::memory_order_relaxed);

  auto task = std::make_shared<Task>();
  task->request = std::move(request);
  task->completion = std::move(completion);
  task->started = std::chrono::steady_clock::now();
  task->wait_timeout_ms = WaitTimeoutMs(task->request);
  task->deadline = task->started +
                   std::chrono::milliseconds(task->wait_timeout_ms);
  std::string reject_error;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_ || stopping_ || workers_.empty()) {
      reject_error = "Policy engine is not running.";
    } else if (queue_.size() >= options_.max_queue) {
      reject_error = "Policy engine queue is full.";
    } else {
      task->id = next_task_id_++;
      queue_.push_back(task);
      inflight_.push_back(task);
      accepted_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  if (!reject_error.empty()) {
    rejected_.fetch_add(1, std::memory_order_relaxed);
    auto result = FallbackResult(task->request, std::move(reject_error), true,
                                 false, ElapsedMs(task->started));
    if (result.used_fallback)
      fallback_.fetch_add(1, std::memory_order_relaxed);
    task->finished.store(true);
    task->completion(std::move(result));
    return false;
  }

  cv_.notify_one();
  deadline_cv_.notify_one();
  return true;
}

void PolicyEnginePool::DeadlineMain() {
  for (;;) {
    std::shared_ptr<Task> expired;
    std::chrono::steady_clock::time_point next_deadline{};
    {
      std::unique_lock<std::mutex> lk(mtx_);
      inflight_.erase(
          std::remove_if(inflight_.begin(), inflight_.end(),
                         [](const std::shared_ptr<Task> &task) {
                           return !task || task->finished.load();
                         }),
          inflight_.end());

      if (stopping_ && inflight_.empty())
        break;

      const auto now = std::chrono::steady_clock::now();
      for (const auto &task : inflight_) {
        if (!task || task->finished.load())
          continue;
        if (task->deadline != std::chrono::steady_clock::time_point{} &&
            now >= task->deadline) {
          expired = task;
          break;
        }
        if (task->deadline != std::chrono::steady_clock::time_point{} &&
            (next_deadline == std::chrono::steady_clock::time_point{} ||
             task->deadline < next_deadline)) {
          next_deadline = task->deadline;
        }
      }

      if (!expired) {
        if (next_deadline == std::chrono::steady_clock::time_point{})
          deadline_cv_.wait(lk, [this]() {
            return stopping_ || !inflight_.empty();
          });
        else
          deadline_cv_.wait_until(lk, next_deadline);
        continue;
      }
    }

    const uint64_t elapsed_ms = ElapsedMs(expired->started);
    auto result =
        FallbackResult(expired->request,
                       "Policy async decision timed out after " +
                           std::to_string(expired->wait_timeout_ms) +
                           "ms.",
                       false, true, elapsed_ms);
    if (auto worker = expired->worker.lock())
      worker->engine.InterruptEvaluation();
    const bool used_fallback = result.used_fallback;
    if (TryComplete(expired, std::move(result))) {
      timed_out_.fetch_add(1, std::memory_order_relaxed);
      if (used_fallback)
        fallback_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void PolicyEnginePool::WorkerMain(const std::shared_ptr<Worker> &worker) {
  for (;;) {
    std::shared_ptr<Task> task;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait(lk, [this]() { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty())
        break;
      task = std::move(queue_.front());
      queue_.pop_front();
      if (task && !task->finished.load()) {
        task->active_counted.store(true);
        ++active_;
      }
    }

    DecisionResult result;
    const auto started = std::chrono::steady_clock::now();
    bool count_failed = false;
    if (!task) {
      result = FallbackResult(DecisionRequest{}, "Empty policy task.", true,
                              false, 0);
      count_failed = true;
    } else if (task->finished.load()) {
      continue;
    } else if (task->deadline != std::chrono::steady_clock::time_point{} &&
               std::chrono::steady_clock::now() >= task->deadline) {
      result = FallbackResult(task->request, "Policy task expired in queue.",
                              false, true, ElapsedMs(task->started));
    } else if (!worker || !worker->engine.Ready()) {
      result = FallbackResult(task->request, "Policy worker is not ready.",
                              true, false, ElapsedMs(started));
      count_failed = true;
    } else {
      task->worker = worker;
      if (task->finished.load()) {
        worker->engine.InterruptEvaluation();
        continue;
      }
      uint32_t eval_timeout_ms = 0;
      {
        std::lock_guard<std::mutex> lk(mtx_);
        eval_timeout_ms = options_.limits.eval_timeout_ms;
      }
      if (task->deadline != std::chrono::steady_clock::time_point{}) {
        const auto now = std::chrono::steady_clock::now();
        if (now < task->deadline) {
          const auto remaining_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  task->deadline - now)
                  .count();
          const auto bounded_remaining_ms =
              remaining_ms <= 0 ? 1 : static_cast<uint32_t>(remaining_ms);
          eval_timeout_ms = eval_timeout_ms == 0
                                ? bounded_remaining_ms
                                : std::min<uint32_t>(eval_timeout_ms,
                                                     bounded_remaining_ms);
        }
      }
      IQuickJS::CallResult call =
          worker->engine.EvaluateJson(task->request.input_json,
                                      eval_timeout_ms, task->deadline,
                                      &task->finished);
      result.worker_id = worker->id;
      result.elapsed_ms = ElapsedMs(started);
      if (task->deadline != std::chrono::steady_clock::time_point{} &&
          std::chrono::steady_clock::now() >= task->deadline) {
        result = FallbackResult(task->request,
                                "Policy task exceeded async deadline.",
                                false, true, ElapsedMs(task->started));
        result.worker_id = worker->id;
      } else if (call.ok) {
        result.ok = true;
        result.output_json = call.output_json.empty() ? std::string("{}")
                                                      : call.output_json;
      } else {
        result = FallbackResult(task->request,
                                call.error.empty()
                                    ? "Policy evaluation failed."
                                    : call.error,
                                false, false, result.elapsed_ms);
        result.worker_id = worker->id;
        count_failed = true;
      }
    }

    const bool count_completed = result.ok;
    const bool count_timeout = result.timed_out;
    const bool count_fallback = result.used_fallback;
    if (TryComplete(task, std::move(result))) {
      if (count_completed)
        completed_.fetch_add(1, std::memory_order_relaxed);
      if (count_failed)
        failed_.fetch_add(1, std::memory_order_relaxed);
      if (count_timeout)
        timed_out_.fetch_add(1, std::memory_order_relaxed);
      if (count_fallback)
        fallback_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

std::string PolicyEnginePool::DiagnosticsJson() const {
  nlohmann::json doc;
  Options snapshot_options;
  std::size_t workers = 0;
  std::size_t queued = 0;
  std::size_t active = 0;
  bool running = false;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    snapshot_options = options_;
    workers = workers_.size();
    queued = queue_.size();
    active = active_;
    running = running_;
  }

  doc = {
      {"running", running},
      {"workers", workers},
      {"queued", queued},
      {"active", active},
      {"maxQueue", snapshot_options.max_queue},
      {"evalTimeoutMs", snapshot_options.limits.eval_timeout_ms},
      {"waitTimeoutMs", snapshot_options.wait_timeout_ms},
      {"failOpen", snapshot_options.fail_open},
      {"entryName", snapshot_options.entry_name},
      {"sourceName", snapshot_options.source_name},
      {"submitted", submitted_.load()},
      {"accepted", accepted_.load()},
      {"rejected", rejected_.load()},
      {"completed", completed_.load()},
      {"failed", failed_.load()},
      {"timedOut", timed_out_.load()},
      {"fallback", fallback_.load()},
  };
  return doc.dump();
}

void PolicyEnginePool::SetConsoleCallback(ConsoleCallback callback) {
  std::lock_guard<std::mutex> lk(mtx_);
  console_callback_ = std::move(callback);
  for (auto &worker : workers_) {
    if (worker)
      worker->engine.SetConsoleCallback(console_callback_);
  }
}

const char *PolicyEnginePool::DefaultEnvInfoPolicyScript() {
  return kDefaultPolicyScript;
}
