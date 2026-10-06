#if !defined(__4F4F1E34_814B_42F7_AB70_A6BC44C0CB2D__)
#define __4F4F1E34_814B_42F7_AB70_A6BC44C0CB2D__

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class IWebSocketServer {
public:
  enum class Mode : uint32_t { Blocking = 0, NonBlocking };
  enum class ConnectType : uint32_t {
    HTTP = 0,
    WS,
  };

  struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
  };

  struct HttpResponse {
    int status = 200;
    std::string body;
    std::string content_type = "application/json;charset=utf-8";
    std::map<std::string, std::string> headers;
  };

  using HttpHandler =
      std::function<int(const HttpRequest &request, HttpResponse &response)>;
  using SendFn = std::function<bool(const std::string &)>;
  using WebSocketOpenHandler =
      std::function<bool(const HttpRequest &request, SendFn send,
                         uint64_t connection_seq, size_t connection_count)>;
  using WebSocketMessageHandler = std::function<void(
      const HttpRequest &request, const std::string &message, SendFn send)>;
  using WebSocketCloseHandler = std::function<void(const HttpRequest &request)>;

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
  explicit IWebSocketServer(const Mode &mode);
  ~IWebSocketServer();

  IWebSocketServer(const IWebSocketServer &) = delete;
  IWebSocketServer &operator=(const IWebSocketServer &) = delete;
  IWebSocketServer(IWebSocketServer &&) = delete;
  IWebSocketServer &operator=(IWebSocketServer &&) = delete;
  bool AddHttpRoute(const std::string &method, const std::string &path,
                    HttpHandler handler);
  void SetFallbackHttpHandler(HttpHandler handler);
  bool AddWebSocketRoute(const std::string &path,
                         WebSocketOpenHandler open_handler,
                         WebSocketMessageHandler message_handler,
                         WebSocketCloseHandler close_handler = nullptr);
  bool Start(const std::string &host, const uint16_t &port);
  bool Start(const uint16_t &port);
  void Stop();
  bool IsRunning() const;
  uint16_t Port() const;
  void Broadcast(const std::string &msg);
  void Poll();
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
/// /*_ Sat, 25 Apr 2026 02:15:02 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__4F4F1E34_814B_42F7_AB70_A6BC44C0CB2D__
