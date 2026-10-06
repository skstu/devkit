#include <libnet_websocket.h>

#include <libnet_ws_server.h>

class IWebSocket::Impl {
public:
  explicit Impl(IWebSocket::Mode mode)
      : server_(mode == IWebSocket::Mode::Blocking
                    ? IWebSocketServer::Mode::Blocking
                    : IWebSocketServer::Mode::NonBlocking) {
  }

  bool Start(uint16_t port) {
    return server_.Start(port);
  }

  void Stop() {
    server_.Stop();
  }

  void Poll() {
    server_.Poll();
  }

  void Broadcast(const std::string &msg) {
    server_.Broadcast(msg);
  }

  void PushBroadcastMsg(const std::string &msg) {
    server_.PushBroadcastMsg(msg);
  }

  bool IsRunning() const {
    return server_.IsRunning();
  }

  uint16_t Port() const {
    return server_.Port();
  }

  void RegisterMessageHandler(tfMessageHandlerCb cb, void *user_data) {
    legacy_message_handler_ = cb;
    legacy_message_handler_user_data_ = user_data;
    server_.RegisterMessageHandler(
        [](const IWebSocketServer::ConnectType &con_type,
           const std::string &source, const std::string &msg,
           std::string &res_data, std::string &res_content_type,
           void *user_data) {
          auto *self = static_cast<Impl *>(user_data);
          if (!self || !self->legacy_message_handler_)
            return;
          const auto legacy_type =
              con_type == IWebSocketServer::ConnectType::HTTP
                  ? IWebSocket::ConnectType::HTTP
                  : IWebSocket::ConnectType::WS;
          self->legacy_message_handler_(
              legacy_type, source, msg, res_data, res_content_type,
              self->legacy_message_handler_user_data_);
        },
        this);
  }

  void RegisterOpenHandler(tfOpenHandlerCb cb) {
    server_.RegisterOpenHandler(std::move(cb));
  }

private:
  IWebSocketServer server_;
  tfMessageHandlerCb legacy_message_handler_ = nullptr;
  void *legacy_message_handler_user_data_ = nullptr;
};

IWebSocket::IWebSocket(const Mode &mode) : mode_(mode) {
  Init();
}

IWebSocket::~IWebSocket() {
  UnInit();
}

void IWebSocket::Init() {
  if (!impl_)
    impl_ = new Impl(mode_);
}

void IWebSocket::UnInit() {
  if (impl_) {
    impl_->Stop();
    delete impl_;
    impl_ = nullptr;
  }
}

bool IWebSocket::Start(const uint16_t &port) {
  Impl *impl = nullptr;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!impl_)
      impl_ = new Impl(mode_);
    impl = impl_;
    if (open_.load())
      return true;
  }

  // Blocking mode owns this call stack until Stop() is called.  Do not hold
  // the facade mutex while starting, otherwise a concurrent Stop() cannot
  // acquire it and the server can never leave its event loop.
  const bool started = impl->Start(port);
  open_.store(started);
  return started;
}

void IWebSocket::Stop() {
  Impl *impl = nullptr;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    impl = impl_;
  }
  if (impl)
    impl->Stop();
  open_.store(false);
}

bool IWebSocket::IsRunning() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ && impl_->IsRunning();
}

uint16_t IWebSocket::Port() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ ? impl_->Port() : 0;
}

void IWebSocket::Broadcast(const std::string &msg) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->Broadcast(msg);
}

void IWebSocket::PushBroadcastMsg(const std::string &msg) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_ && !msg.empty())
    impl_->PushBroadcastMsg(msg);
}

void IWebSocket::Poll() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (open_.load() && impl_)
    impl_->Poll();
}

void IWebSocket::RegisterMessageHandler(tfMessageHandlerCb cb,
                                        void *user_data) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterMessageHandler(cb, user_data);
}

void IWebSocket::RegisterOpenHandler(tfOpenHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterOpenHandler(std::move(cb));
}
