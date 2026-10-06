#include <libnet_ws_server.h>

#include <algorithm>
#include <cctype>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <libusockets.h>
#include <uwebsockets/App.h>

namespace {
constexpr const char kDefaultContentType[] = "application/json;charset=utf-8";

std::string NormalizePath(std::string path) {
  const auto query = path.find_first_of("?#");
  if (query != std::string::npos)
    path.resize(query);
  if (path.size() > 1 && path.back() == '/')
    path.pop_back();
  return path.empty() ? "/" : path;
}

std::string NormalizeMethod(std::string method) {
  std::transform(
      method.begin(), method.end(), method.begin(),
      [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return method;
}

IWebSocketServer::HttpRequest MakeRequest(uWS::HttpRequest *req) {
  IWebSocketServer::HttpRequest out;
  if (!req)
    return out;
  out.method = NormalizeMethod(std::string(req->getMethod()));
  out.path = NormalizePath(std::string(req->getUrl()));
  for (auto header : *req)
    out.headers.emplace(std::string(header.first), std::string(header.second));
  return out;
}
} // namespace

// Server backend: uWebSockets (uSockets event loop, one dedicated thread per
// IWebSocketServer instance). uWebSockets does not ship a usable WebSocket
// *client* (its ClientApp is an unimplemented stub), so IWebSocketClient
// keeps its existing libhv-backed implementation in libws_client.cc.
class IWebSocketServer::Impl {
public:
  Impl() = default;

  ~Impl() {
    stop();
  }

  bool AddHttpRoute(const std::string &method, const std::string &path,
                    HttpHandler handler) {
    if (method.empty() || path.empty() || !handler)
      return false;
    std::lock_guard<std::mutex> lk(lock_);
    if (running_)
      return false;
    const std::string normalized_method = NormalizeMethod(method);
    const std::string normalized_path = NormalizePath(path);
    const auto duplicate = std::find_if(
        http_routes_.begin(), http_routes_.end(),
        [&normalized_method, &normalized_path](const HttpRoute &route) {
          return route.method == normalized_method &&
                 route.path == normalized_path;
        });
    if (duplicate != http_routes_.end())
      return false;
    http_routes_.push_back({std::move(normalized_method),
                            std::move(normalized_path), std::move(handler)});
    return true;
  }

  void SetFallbackHttpHandler(HttpHandler handler) {
    std::lock_guard<std::mutex> lk(lock_);
    if (!running_)
      fallback_http_handler_ = std::move(handler);
  }

  bool AddWebSocketRoute(const std::string &path,
                         WebSocketOpenHandler open_handler,
                         WebSocketMessageHandler message_handler,
                         WebSocketCloseHandler close_handler) {
    if (path.empty())
      return false;
    std::lock_guard<std::mutex> lk(lock_);
    if (running_)
      return false;
    const std::string normalized_path = NormalizePath(path);
    const auto duplicate =
        std::find_if(websocket_routes_.begin(), websocket_routes_.end(),
                     [&normalized_path](const WebSocketRoute &route) {
                       return route.path == normalized_path;
                     });
    if (duplicate != websocket_routes_.end())
      return false;
    websocket_routes_.push_back({normalized_path, std::move(open_handler),
                                 std::move(message_handler),
                                 std::move(close_handler)});
    return true;
  }

  bool Configure(const std::string &host, unsigned short port) {
    host_ = host;
    requested_port_ = port;
    return true;
  }

  bool run_blocking() {
    return start(true);
  }

  bool run_non_blocking() {
    return start(false);
  }

  void poll() {
    // uWebSockets drives its own dedicated loop thread; nothing to pump here.
  }

  void RegisterMessageHandler(tfMessageHandlerCb cb, void *user_data) {
    std::lock_guard<std::mutex> lk(lock_);
    legacy_message_handler_ = cb;
    legacy_message_handler_user_data_ = user_data;
  }

  void RegisterOpenHandler(tfOpenHandlerCb cb) {
    std::lock_guard<std::mutex> lk(lock_);
    legacy_open_handler_ = std::move(cb);
  }

  void stop() {
    uWS::Loop *loop = nullptr;
    {
      std::lock_guard<std::mutex> lk(lock_);
      loop = loop_;
    }
    if (loop)
      loop->defer([this]() { CloseAll(); });
    // Stop can be called from a server callback. Joining the event-loop
    // thread from itself would throw (and used to make that path unusable).
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
      thread_.join();

    std::lock_guard<std::mutex> lk(lock_);
    running_ = false;
    loop_ = nullptr;
    listen_socket_ = nullptr;
    bound_port_ = 0;
  }

  void broadcast(const std::string &msg) {
    push_broadcast_msg(msg);
  }

  void push_broadcast_msg(const std::string &msg) {
    uWS::Loop *loop = nullptr;
    {
      std::lock_guard<std::mutex> lk(lock_);
      loop = loop_;
    }
    if (!loop)
      return;
    loop->defer([this, msg]() {
      std::vector<void *> sockets;
      {
        std::lock_guard<std::mutex> lk(lock_);
        sockets = sockets_;
      }
      for (void *raw : sockets)
        static_cast<WS *>(raw)->send(msg, uWS::OpCode::TEXT);
    });
  }

  bool is_running() const {
    std::lock_guard<std::mutex> lk(lock_);
    return running_;
  }

  uint16_t port() const {
    std::lock_guard<std::mutex> lk(lock_);
    return bound_port_;
  }

private:
  struct HttpRoute {
    std::string method;
    std::string path;
    HttpHandler handler;
  };

  struct WebSocketRoute {
    std::string path;
    WebSocketOpenHandler open_handler;
    WebSocketMessageHandler message_handler;
    WebSocketCloseHandler close_handler;
  };

  struct PerSocketData {
    uint64_t seq = 0;
  };

  using WS = uWS::WebSocket<false, true, PerSocketData>;

  bool start(bool wait) {
    {
      std::lock_guard<std::mutex> lk(lock_);
      if (running_)
        return true;
    }

    if (wait)
      return RunLoop(nullptr);

    auto ready = std::make_shared<std::promise<bool>>();
    auto ready_future = ready->get_future();
    thread_ = std::thread([this, ready]() { RunLoop(ready.get()); });
    const bool ok = ready_future.get();
    if (!ok && thread_.joinable())
      thread_.join();
    return ok;
  }

  // Runs the uWebSockets event loop to completion on the calling thread.
  // Reports listen success via `ready` (when non-null) before blocking in
  // app.run(). Returns whether the listen attempt succeeded.
  bool RunLoop(std::promise<bool> *ready) {
    uWS::App app;
    BindHttpRoutes(app);
    BindWebSocketRoutes(app);

    bool ok = false;
    us_listen_socket_t *listen_socket = nullptr;
    app.listen(host_, static_cast<int>(requested_port_),
               [&](us_listen_socket_t *token) {
                 listen_socket = token;
                 ok = token != nullptr;
               });

    if (ok) {
      std::lock_guard<std::mutex> lk(lock_);
      listen_socket_ = listen_socket;
      loop_ = app.getLoop();
      bound_port_ = static_cast<uint16_t>(us_socket_local_port(
          0, reinterpret_cast<struct us_socket_t *>(listen_socket)));
      running_ = true;
    }
    if (ready)
      ready->set_value(ok);
    if (!ok)
      return false;

    app.run();

    std::lock_guard<std::mutex> lk(lock_);
    running_ = false;
    loop_ = nullptr;
    listen_socket_ = nullptr;
    bound_port_ = 0;
    return true;
  }

  // Deferred onto the server's own loop thread by stop()/push_broadcast_msg.
  void CloseAll() {
    us_listen_socket_t *listen_socket = nullptr;
    std::vector<void *> sockets;
    {
      std::lock_guard<std::mutex> lk(lock_);
      listen_socket = listen_socket_;
      listen_socket_ = nullptr;
      sockets = sockets_;
    }
    if (listen_socket)
      us_listen_socket_close(0, listen_socket);
    for (void *raw : sockets)
      static_cast<WS *>(raw)->end(1001, "server stopping");
  }

  void BindHttpRoutes(uWS::App &app) {
    std::vector<std::string> patterns;
    for (const auto &route : http_routes_) {
      if (std::find(patterns.begin(), patterns.end(), route.path) ==
          patterns.end())
        patterns.push_back(route.path);
    }
    for (const auto &pattern : patterns) {
      app.any(pattern,
              [this](auto *res, auto *req) { DispatchHttp(res, req); });
    }
    app.any("/*", [this](auto *res, auto *req) { DispatchHttp(res, req); });
  }

  void DispatchHttp(uWS::HttpResponse<false> *res, uWS::HttpRequest *req) {
    auto request = std::make_shared<HttpRequest>(MakeRequest(req));

    HttpHandler matched;
    HttpHandler fallback;
    tfMessageHandlerCb legacy_handler = nullptr;
    void *legacy_user_data = nullptr;
    {
      std::lock_guard<std::mutex> lk(lock_);
      for (const auto &route : http_routes_) {
        if (route.path == request->path && route.method == request->method) {
          matched = route.handler;
          break;
        }
      }
      fallback = fallback_http_handler_;
      legacy_handler = legacy_message_handler_;
      legacy_user_data = legacy_message_handler_user_data_;
    }

    res->onAborted([]() {});
    auto body = std::make_shared<std::string>();
    res->onData([res, request, matched, fallback, legacy_handler,
                 legacy_user_data, body](std::string_view chunk, bool last) {
      body->append(chunk);
      if (!last)
        return;
      request->body = *body;

      HttpResponse response;
      if (matched) {
        const int status = matched(*request, response);
        if (response.status == 200)
          response.status = status > 0 ? status : 200;
      } else if (fallback) {
        const int status = fallback(*request, response);
        if (response.status == 200)
          response.status = status > 0 ? status : 200;
      } else if (legacy_handler) {
        std::string content_type = kDefaultContentType;
        std::string legacy_body = "{}";
        legacy_handler(IWebSocketServer::ConnectType::HTTP, request->path,
                       request->body, legacy_body, content_type,
                       legacy_user_data);
        response.status = 200;
        response.body = legacy_body.empty() ? "{}" : legacy_body;
        response.content_type =
            content_type.empty() ? kDefaultContentType : content_type;
      } else {
        response.status = 404;
        response.body = "{}";
      }

      res->writeStatus(std::to_string(response.status));
      for (const auto &header : response.headers)
        res->writeHeader(header.first, header.second);
      res->writeHeader("Content-Type", response.content_type.empty()
                                           ? kDefaultContentType
                                           : response.content_type);
      res->end(response.body);
    });
  }

  void BindWebSocketRoutes(uWS::App &app) {
    for (size_t idx = 0; idx < websocket_routes_.size(); ++idx) {
      typename uWS::App::template WebSocketBehavior<PerSocketData> behavior{};
      behavior.compression = uWS::DISABLED;
      behavior.open = [this, idx](WS *ws) { OnOpen(ws, idx); };
      behavior.message = [this, idx](WS *ws, std::string_view msg,
                                     uWS::OpCode) { OnMessage(ws, idx, msg); };
      behavior.close = [this, idx](WS *ws, int, std::string_view) {
        OnClose(ws, idx);
      };
      app.ws<PerSocketData>(websocket_routes_[idx].path, std::move(behavior));
    }
  }

  void OnOpen(WS *ws, size_t idx) {
    uint64_t seq = 0;
    size_t count = 0;
    tfOpenHandlerCb legacy_open;
    WebSocketOpenHandler open_handler;
    std::string path;
    {
      std::lock_guard<std::mutex> lk(lock_);
      sockets_.push_back(ws);
      seq = ++open_sequence_;
      count = sockets_.size();
      legacy_open = legacy_open_handler_;
      if (idx < websocket_routes_.size()) {
        open_handler = websocket_routes_[idx].open_handler;
        path = websocket_routes_[idx].path;
      }
    }
    ws->getUserData()->seq = seq;

    HttpRequest request;
    request.path = path;
    SendFn send = [ws](const std::string &msg) {
      ws->send(msg, uWS::OpCode::TEXT);
      return true;
    };

    bool accepted = true;
    if (open_handler)
      accepted = open_handler(request, send, seq, count);
    if (accepted && legacy_open)
      legacy_open(seq, count, send);
    if (!accepted)
      ws->end(1008, "rejected");
  }

  void OnMessage(WS *ws, size_t idx, std::string_view msg) {
    WebSocketMessageHandler route_handler;
    tfMessageHandlerCb legacy_handler = nullptr;
    void *legacy_user_data = nullptr;
    std::string path;
    {
      std::lock_guard<std::mutex> lk(lock_);
      legacy_handler = legacy_message_handler_;
      legacy_user_data = legacy_message_handler_user_data_;
      if (idx < websocket_routes_.size()) {
        route_handler = websocket_routes_[idx].message_handler;
        path = websocket_routes_[idx].path;
      }
    }

    HttpRequest request;
    request.path = path;
    const std::string message(msg);
    SendFn send = [ws](const std::string &out) {
      ws->send(out, uWS::OpCode::TEXT);
      return true;
    };

    if (route_handler) {
      route_handler(request, message, send);
      return;
    }
    if (legacy_handler) {
      std::string content_type = kDefaultContentType;
      std::string response;
      legacy_handler(IWebSocketServer::ConnectType::WS, request.path, message,
                     response, content_type, legacy_user_data);
      if (!response.empty())
        send(response);
    }
  }

  void OnClose(WS *ws, size_t idx) {
    WebSocketCloseHandler close_handler;
    std::string path;
    {
      std::lock_guard<std::mutex> lk(lock_);
      sockets_.erase(std::remove(sockets_.begin(), sockets_.end(), ws),
                     sockets_.end());
      if (idx < websocket_routes_.size()) {
        close_handler = websocket_routes_[idx].close_handler;
        path = websocket_routes_[idx].path;
      }
    }
    if (close_handler) {
      HttpRequest request;
      request.path = path;
      close_handler(request);
    }
  }

  mutable std::mutex lock_;
  std::thread thread_;
  uWS::Loop *loop_ = nullptr;
  us_listen_socket_t *listen_socket_ = nullptr;
  std::vector<void *> sockets_;
  std::vector<HttpRoute> http_routes_;
  std::vector<WebSocketRoute> websocket_routes_;
  HttpHandler fallback_http_handler_;
  std::atomic_uint64_t open_sequence_{0};
  std::string host_;
  unsigned short requested_port_ = 0;
  uint16_t bound_port_ = 0;
  bool running_ = false;
  tfMessageHandlerCb legacy_message_handler_ = nullptr;
  void *legacy_message_handler_user_data_ = nullptr;
  tfOpenHandlerCb legacy_open_handler_;
};

IWebSocketServer::IWebSocketServer(const Mode &mode) : mode_(mode) {
  Init();
}

IWebSocketServer::~IWebSocketServer() {
  UnInit();
}

void IWebSocketServer::Init() {
  if (!impl_)
    impl_ = new Impl();
}

void IWebSocketServer::UnInit() {
  if (impl_) {
    impl_->stop();
    delete impl_;
    impl_ = nullptr;
  }
}

bool IWebSocketServer::AddHttpRoute(const std::string &method,
                                    const std::string &path,
                                    HttpHandler handler) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!impl_)
    impl_ = new Impl();
  return impl_->AddHttpRoute(method, path, std::move(handler));
}

void IWebSocketServer::SetFallbackHttpHandler(HttpHandler handler) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!impl_)
    impl_ = new Impl();
  impl_->SetFallbackHttpHandler(std::move(handler));
}

bool IWebSocketServer::AddWebSocketRoute(
    const std::string &path, WebSocketOpenHandler open_handler,
    WebSocketMessageHandler message_handler,
    WebSocketCloseHandler close_handler) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!impl_)
    impl_ = new Impl();
  return impl_->AddWebSocketRoute(path, std::move(open_handler),
                                  std::move(message_handler),
                                  std::move(close_handler));
}

bool IWebSocketServer::Start(const std::string &host, const uint16_t &port) {
  Impl *impl = nullptr;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!impl_)
      impl_ = new Impl();
    impl = impl_;
  }
  // Blocking mode runs the event loop on this call stack until Stop() is
  // invoked from another thread, so mtx_ must not be held here -- otherwise
  // a concurrent Stop() call would deadlock waiting for the same lock.
  impl->Configure(host, port);
  const bool ok =
      mode_ == Mode::Blocking ? impl->run_blocking() : impl->run_non_blocking();
  open_.store(ok);
  return ok;
}

bool IWebSocketServer::Start(const uint16_t &port) {
  return Start(std::string(), port);
}

void IWebSocketServer::Stop() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->stop();
  open_.store(false);
}

bool IWebSocketServer::IsRunning() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ && impl_->is_running();
}

uint16_t IWebSocketServer::Port() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ ? impl_->port() : 0;
}

void IWebSocketServer::Broadcast(const std::string &msg) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->broadcast(msg);
}

void IWebSocketServer::Poll() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->poll();
}

void IWebSocketServer::PushBroadcastMsg(const std::string &msg) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_ && !msg.empty())
    impl_->push_broadcast_msg(msg);
}

void IWebSocketServer::RegisterMessageHandler(tfMessageHandlerCb cb,
                                              void *user_data) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterMessageHandler(cb, user_data);
}

void IWebSocketServer::RegisterOpenHandler(tfOpenHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterOpenHandler(std::move(cb));
}
