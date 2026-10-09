#include "../common/backend.hpp"
#include <atomic>
#include <jni.h>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace {
JavaVM *vm = nullptr;
jclass engineClass = nullptr;
jobject application = nullptr;
std::mutex gate;
struct Environment {
  JNIEnv *env = nullptr;
  bool detach = false;
  Environment() {
    if (!vm)
      return;
    auto status = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
      if (vm->AttachCurrentThread(&env, nullptr) == 0)
        detach = true;
      else
        env = nullptr;
    }
  }
  ~Environment() {
    if (detach)
      vm->DetachCurrentThread();
  }
  bool valid() {
    if (!env)
      return false;
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return false;
    }
    return true;
  }
};
struct State {
  dkble::Emit emit;
};
std::map<jlong, std::weak_ptr<State>> states;
std::atomic<jlong> next{1};
std::string text(JNIEnv *e, jstring s) {
  if (!s)
    return {};
  const char *p = e->GetStringUTFChars(s, nullptr);
  if (!p)
    return {};
  std::string value(p);
  e->ReleaseStringUTFChars(s, p);
  return value;
}
} // namespace
extern "C" JNIEXPORT jint JNICALL
Java_com_skstu_devkit_ble_BleRuntime_nativeInitialize(JNIEnv *e, jobject,
                                                      jobject context,
                                                      jclass cls) {
  std::lock_guard lock(gate);
  if (engineClass)
    return 0;
  if (!context || !cls || e->GetJavaVM(&vm) != 0)
    return DKBLE_INVALID;
  auto newClass = static_cast<jclass>(e->NewGlobalRef(cls));
  auto newContext = e->NewGlobalRef(context);
  if (!newClass || !newContext) {
    if (newClass)
      e->DeleteGlobalRef(newClass);
    if (newContext)
      e->DeleteGlobalRef(newContext);
    return DKBLE_IO;
  }
  engineClass = newClass;
  application = newContext;
  return 0;
}
extern "C" JNIEXPORT void JNICALL
Java_com_skstu_devkit_ble_BleEngine_nativeEvent(
    JNIEnv *e, jobject, jlong id, jlong generation, jint type, jlong request,
    jint status, jstring link, jstring peer, jstring probe, jstring detail,
    jbyteArray data, jint rssi, jboolean initiator) {
  std::shared_ptr<State> state;
  {
    std::lock_guard lock(gate);
    auto found = states.find(id);
    if (found != states.end())
      state = found->second.lock();
  }
  if (!state)
    return;
  dkble::Event event;
  event.type = type;
  event.generation = generation;
  event.request = request;
  event.status = status;
  event.rssi = rssi;
  event.initiator = initiator;
  event.link = text(e, link);
  event.peer = text(e, peer);
  event.probe = text(e, probe);
  event.detail = text(e, detail);
  if (data) {
    auto size = e->GetArrayLength(data);
    if (size < 0 || size > 512)
      return;
    event.data.resize(size);
    if (size)
      e->GetByteArrayRegion(data, 0, size,
                            reinterpret_cast<jbyte *>(event.data.data()));
  }
  if (e->ExceptionCheck()) {
    e->ExceptionClear();
    return;
  }
  state->emit(std::move(event));
}
namespace dkble {
class AndroidBackend final : public Backend {
  jobject engine = nullptr;
  jmethodID commandMethod = nullptr, closeMethod = nullptr,
            optionsMethod = nullptr, readMethod = nullptr;
  jlong id = next++;
  std::shared_ptr<State> state;

public:
  AndroidBackend(Config c, Emit emit) : state(std::make_shared<State>()) {
    Environment e;
    if (!e.valid())
      throw std::runtime_error("JNI unavailable");
    state->emit = std::move(emit);
    jclass cls;
    jobject context;
    {
      std::lock_guard lock(gate);
      cls = engineClass;
      context = application;
    }
    if (!cls || !context)
      throw std::runtime_error("BleRuntime.initialize required");
    auto constructor =
        e.env->GetMethodID(cls, "<init>",
                           "(Landroid/content/Context;JLjava/lang/String;Ljava/"
                           "lang/String;Ljava/lang/String;Z)V");
    commandMethod = e.env->GetMethodID(
        cls, "command", "(IJLjava/lang/String;Ljava/lang/String;[BJ)I");
    closeMethod = e.env->GetMethodID(cls, "close", "()I");
    optionsMethod = e.env->GetMethodID(cls, "setOptions", "([I)I");
    readMethod = e.env->GetMethodID(cls, "setReadInterval", "(I)I");
    if (!constructor || !commandMethod || !closeMethod || !optionsMethod ||
        !readMethod || !e.valid())
      throw std::runtime_error("JNI contract mismatch");
    auto service = e.env->NewStringUTF(c.service.c_str()),
         rx = e.env->NewStringUTF(c.receive.c_str()),
         tx = e.env->NewStringUTF(c.notify.c_str());
    auto local = e.env->NewObject(cls, constructor, context, id, service, rx,
                                  tx, c.hints ? JNI_TRUE : JNI_FALSE);
    e.env->DeleteLocalRef(service);
    e.env->DeleteLocalRef(rx);
    e.env->DeleteLocalRef(tx);
    if (!local || !e.valid())
      throw std::runtime_error("JNI engine failed");
    engine = e.env->NewGlobalRef(local);
    e.env->DeleteLocalRef(local);
    if (!engine)
      throw std::runtime_error("JNI allocation failed");
    {
      std::lock_guard lock(gate);
      states[id] = state;
    }
  }
  ~AndroidBackend() override { close(); }
  void close() override {
    {
      std::lock_guard lock(gate);
      states.erase(id);
    }
    state.reset();
    if (engine) {
      Environment e;
      if (e.env) {
        e.env->CallIntMethod(engine, closeMethod);
        e.valid();
        e.env->DeleteGlobalRef(engine);
      }
      engine = nullptr;
    }
  }
  int set_read_interval(uint32_t interval) override {
    Environment e;
    if (!e.valid())
      return DKBLE_IO;
    auto result =
        e.env->CallIntMethod(engine, readMethod, static_cast<jint>(interval));
    return e.valid() ? result : DKBLE_IO;
  }
  int set_options(const dkble_options &options) override {
    Environment e;
    if (!e.valid())
      return DKBLE_IO;
    const jint fields[] = {static_cast<jint>(options.connect_timeout_ms),
                           static_cast<jint>(options.send_timeout_ms),
                           static_cast<jint>(options.retry_delay_ms),
                           static_cast<jint>(options.candidate_ttl_ms),
                           static_cast<jint>(options.maximum_links),
                           static_cast<jint>(options.maximum_candidates)};
    auto values = e.env->NewIntArray(6);
    if (!values)
      return DKBLE_IO;
    e.env->SetIntArrayRegion(values, 0, 6, fields);
    auto result = e.env->CallIntMethod(engine, optionsMethod, values);
    e.env->DeleteLocalRef(values);
    return e.valid() ? result : DKBLE_IO;
  }
  int command(Command c) override {
    if (!engine)
      return DKBLE_STATE;
    Environment e;
    if (!e.valid())
      return DKBLE_IO;
    auto value = e.env->NewStringUTF(c.value.c_str()),
         extra = e.env->NewStringUTF(c.extra.c_str());
    auto data = e.env->NewByteArray(static_cast<jsize>(c.data.size()));
    if (!c.data.empty() && data)
      e.env->SetByteArrayRegion(data, 0, static_cast<jsize>(c.data.size()),
                                reinterpret_cast<const jbyte *>(c.data.data()));
    int result = DKBLE_IO;
    if (value && extra && data && e.valid())
      result =
          e.env->CallIntMethod(engine, commandMethod, static_cast<jint>(c.op),
                               static_cast<jlong>(c.generation), value, extra,
                               data, static_cast<jlong>(c.request));
    if (value)
      e.env->DeleteLocalRef(value);
    if (extra)
      e.env->DeleteLocalRef(extra);
    if (data)
      e.env->DeleteLocalRef(data);
    return e.valid() ? result : DKBLE_IO;
  }
};
std::unique_ptr<Backend> make_backend(Config c, Emit e, Wake) {
  {
    std::lock_guard lock(gate);
    if (!engineClass || !application)
      return {};
  }
  return std::make_unique<AndroidBackend>(std::move(c), std::move(e));
}
} // namespace dkble
