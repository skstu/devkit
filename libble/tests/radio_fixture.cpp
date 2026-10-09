#include "radio_fixture.hpp"
#include <chrono>
#include <thread>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif
int main(int argc, char **argv) {
  if (argc != 5) {
    std::fputs("Usage: ble_radio_fixture scan|advertise macos|windows|linux "
               "ROUND SECONDS\n",
               stderr);
    return 2;
  }
  std::string mode = argv[1];
  if (mode != "scan" && mode != "advertise")
    return 2;
  int seconds = std::atoi(argv[4]);
  if (seconds < 5 || seconds > 180)
    return 2;
  blelab::Fixture fixture(argv[2], argv[3], [](const std::string &line) {
    std::puts(line.c_str());
    std::fflush(stdout);
  });
  if (!fixture.start(mode == "advertise"))
    return 1;
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < deadline) {
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    fixture.step();
  }
  return fixture.finish();
}
