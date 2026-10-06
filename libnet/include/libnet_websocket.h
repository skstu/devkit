#if !defined(__AAD461A1_7148_435C_A5FB_B0F532B9C38F__)
#define __AAD461A1_7148_435C_A5FB_B0F532B9C38F__
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <set>
class IWebSocket {
public:
  enum class Mode : uint32_t { Blocking = 0, NonBlocking };
  enum class ConnectType : uint32_t {
    HTTP = 0,
    WS,
  };
  typedef void (*tfMessageHandlerCb)(const ConnectType &conType,
                                     const std::string &source /*path*/,
                                     const std::string &msg,
                                     std::string &resData,
                                     std::string &resContentType,
                                     void *user_data);
  // Called when a new WS client connects.
  // connection_seq: monotonically increasing sequence for diagnostics.
  // connection_count: current number of active WS clients after this open.
  // sendFn: call it with a JSON string to push a message to that client only.
  //         Returns true when the send succeeded.
  typedef std::function<void(uint64_t connection_seq, size_t connection_count,
                             std::function<bool(const std::string &)>)>
      tfOpenHandlerCb;

public:
  explicit IWebSocket(const Mode &mode);
  ~IWebSocket();

  IWebSocket(const IWebSocket &) = delete;
  IWebSocket &operator=(const IWebSocket &) = delete;
  IWebSocket(IWebSocket &&) = delete;
  IWebSocket &operator=(IWebSocket &&) = delete;

  bool Start(const uint16_t &port);
  void Stop();
  bool IsRunning() const;
  uint16_t Port() const;
  void Poll();
  void Broadcast(const std::string &msg);
  void PushBroadcastMsg(const std::string &);
  void RegisterMessageHandler(tfMessageHandlerCb cb, void *user_data);
  void RegisterOpenHandler(tfOpenHandlerCb cb);

private:
  void Init();
  void UnInit();
  class Impl;
  const Mode mode_;
  Impl *impl_ = nullptr;
  std::atomic<bool> open_ = false;
  mutable std::mutex mtx_;
};

/// /*_ Memade®（新生™） _**/
/// /*_ Sat, 25 Apr 2026 02:33:00 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__AAD461A1_7148_435C_A5FB_B0F532B9C38F__
