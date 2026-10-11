#include <libble/ble.h>
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Exercise the real public ABI and Android backend with a failing JVM method
// lookup. No radio, Android identity, application data or Java runtime needed.
extern "C" jint Java_com_skstu_devkit_ble_BleRuntime_nativeInitialize(
    JNIEnv *, jobject, jobject, jclass);
namespace {
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "CHECK %d: %s\n", __LINE__, #v); std::abort(); } } while (0)
JNIEnv environment{};
JavaVM machine{};
const char *missing = nullptr;
bool pending = false;
int lookups = 0, lookupsWithException = 0, cleared = 0;
jint getVM(JNIEnv *, JavaVM **out) { *out = &machine; return JNI_OK; }
jint getEnv(JavaVM *, void **out, jint) { *out = &environment; return JNI_OK; }
jobject globalRef(JNIEnv *, jobject value) { return value; }
jboolean exceptionCheck(JNIEnv *) { return pending ? JNI_TRUE : JNI_FALSE; }
void exceptionClear(JNIEnv *) { pending = false; ++cleared; }
jmethodID method(JNIEnv *, jclass, const char *name, const char *) {
  ++lookups;
  if (pending) ++lookupsWithException;
  if (!std::strcmp(name, missing)) { pending = true; return nullptr; }
  return reinterpret_cast<jmethodID>(1);
}
}
int main() {
  JNINativeInterface functions{};
  functions.GetJavaVM = getVM;
  functions.NewGlobalRef = globalRef;
  functions.ExceptionCheck = exceptionCheck;
  functions.ExceptionClear = exceptionClear;
  functions.GetMethodID = method;
  JNIInvokeInterface calls{};
  calls.GetEnv = getEnv;
  environment.functions = &functions;
  machine.functions = &calls;
  CHECK(Java_com_skstu_devkit_ble_BleRuntime_nativeInitialize(
      &environment, nullptr, reinterpret_cast<jobject>(1),
      reinterpret_cast<jclass>(2)) == DKBLE_OK);
  dkble_config config{};
  config.struct_size = sizeof(config);
  config.abi_version = DKBLE_ABI_VERSION;
  config.service_uuid = "10c94001-68c0-4f44-a1ea-f18c76305000";
  config.receive_uuid = "10c94002-68c0-4f44-a1ea-f18c76305000";
  config.notify_uuid = "10c94003-68c0-4f44-a1ea-f18c76305000";
  const char *names[] = {"<init>", "command", "close", "setOptions", "setReadInterval"};
  for (int i = 0; i < 5; ++i) {
    missing = names[i];
    lookups = 0;
    dkble_context *context = reinterpret_cast<dkble_context *>(1);
    CHECK(dkble_create(&config, &context) == DKBLE_IO);
    CHECK(!context && !pending && lookupsWithException == 0);
    CHECK(lookups == i + 1 && cleared == i + 1);
  }
  std::puts("PASS: all five JNI lookup failures return IO, null context, cleared exception, no subsequent lookup");
}
