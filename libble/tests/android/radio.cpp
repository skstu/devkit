#include "../radio_fixture.hpp"
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <jni.h>
#include <thread>
namespace {
std::atomic<bool> active{false}, stop{false};
std::string text(JNIEnv *e, jstring v) {
  const char *p = e->GetStringUTFChars(v, nullptr);
  if (!p)
    return {};
  std::string out(p);
  e->ReleaseStringUTFChars(v, p);
  return out;
}
} // namespace
extern "C" JNIEXPORT jint JNICALL
Java_com_skstu_devkit_blelab_RadioControl_nativeStart(JNIEnv *e, jobject,
                                                       jstring role,
                                                       jstring round,
                                                       jint seconds, jint warm) {
  if (seconds < 5 || seconds > 180 || warm < 0 || warm > 3 || active.exchange(true))
    return -3;
  auto mode = text(e, role), nonce = text(e, round);
  stop = false;
  std::thread([mode, nonce, seconds, warm] {
    blelab::Fixture fixture("android", nonce, [](const std::string &line) {
      __android_log_print(ANDROID_LOG_INFO, "DKBLELab", "%s", line.c_str());
    }, static_cast<unsigned>(warm));
    if (fixture.start(mode == "advertise")) {
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
      while (!stop && std::chrono::steady_clock::now() < deadline) {
        fixture.step();
        fixture.warmStep();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      fixture.finish();
    }
    active = false;
  }).detach();
  return 0;
}
extern "C" JNIEXPORT void JNICALL
Java_com_skstu_devkit_blelab_RadioControl_nativeStop(JNIEnv *, jobject) {
  stop = true;
}
