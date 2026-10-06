#if !defined(__F79C0F91_5607_4573_AF61_20BE011865BA__)
#define __F79C0F91_5607_4573_AF61_20BE011865BA__

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class IWebSocketClient {
public:
  enum class Mode : uint32_t { Blocking = 0, NonBlocking };
  typedef std::function<void()> tfOpenHandlerCb;
  typedef std::function<void(const std::string &)> tfMessageHandlerCb;
  typedef std::function<void(uint16_t, const std::string &)> tfCloseHandlerCb;
  typedef std::function<void(const std::string &)> tfFailHandlerCb;

public:
  explicit IWebSocketClient(const Mode &mode = Mode::NonBlocking);
  ~IWebSocketClient();

  IWebSocketClient(const IWebSocketClient &) = delete;
  IWebSocketClient &operator=(const IWebSocketClient &) = delete;
  IWebSocketClient(IWebSocketClient &&) = delete;
  IWebSocketClient &operator=(IWebSocketClient &&) = delete;

  bool Connect(const std::string &uri);
  void Close(uint16_t code = 1000, const std::string &reason = "Normal close");
  bool SendText(const std::string &msg);
  void Poll();
  bool IsOpen() const;

  void RegisterOpenHandler(tfOpenHandlerCb cb);
  void RegisterMessageHandler(tfMessageHandlerCb cb);
  void RegisterCloseHandler(tfCloseHandlerCb cb);
  void RegisterFailHandler(tfFailHandlerCb cb);

private:
  class Impl;
  const Mode mode_;
  Impl *impl_ = nullptr;
  mutable std::mutex mtx_;
};

/// /*_ Memade®（新生™） _**/
/// /*_ Sat, 25 Apr 2026 02:33:00 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__F79C0F91_5607_4573_AF61_20BE011865BA__
