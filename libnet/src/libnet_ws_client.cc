#include <libnet_ws_client.h>

#include <hv/EventLoopThread.h>
#include <hv/WebSocketClient.h>

class IWebSocketClient::Impl {
public:
  explicit Impl(IWebSocketClient::Mode mode) : mode_(mode) {
    if (mode_ == IWebSocketClient::Mode::NonBlocking) {
      loop_thread_ = std::make_shared<hv::EventLoopThread>();
      client_ = std::make_unique<hv::WebSocketClient>(loop_thread_->loop());
    } else {
      client_ = std::make_unique<hv::WebSocketClient>();
    }

    client_->onopen = [this]() {
      tfOpenHandlerCb cb;
      {
        std::lock_guard<std::mutex> lk(lock_);
        connected_ = true;
        connecting_ = false;
        cb = open_handler_;
      }
      if (cb)
        cb();
    };

    client_->onmessage = [this](const std::string &msg) {
      tfMessageHandlerCb cb;
      {
        std::lock_guard<std::mutex> lk(lock_);
        cb = message_handler_;
      }
      if (cb)
        cb(msg);
    };

    client_->onclose = [this]() {
      tfCloseHandlerCb cb;
      {
        std::lock_guard<std::mutex> lk(lock_);
        connected_ = false;
        connecting_ = false;
        cb = close_handler_;
      }
      if (cb)
        cb(1000, "closed");
    };
  }

  ~Impl() {
    Close(1000, "Normal close");
  }

  bool Connect(const std::string &uri) {
    if (uri.empty())
      return false;

    {
      std::lock_guard<std::mutex> lk(lock_);
      if (connected_ || connecting_)
        return true;
      connecting_ = true;
      last_error_.clear();
    }

    if (loop_thread_)
      loop_thread_->start();

    const int rc = client_->open(uri.c_str());
    if (rc != 0) {
      tfFailHandlerCb cb;
      {
        std::lock_guard<std::mutex> lk(lock_);
        connected_ = false;
        connecting_ = false;
        last_error_ = "websocket open failed: " + std::to_string(rc);
        cb = fail_handler_;
      }
      if (cb)
        cb(last_error_);
      return false;
    }

    if (mode_ == IWebSocketClient::Mode::Blocking)
      client_->loop()->run();
    return true;
  }

  void Close(uint16_t code, const std::string &reason) {
    (void)code;
    (void)reason;
    bool should_close = false;
    {
      std::lock_guard<std::mutex> lk(lock_);
      should_close = connected_ || connecting_;
      connected_ = false;
      connecting_ = false;
    }
    if (should_close && client_)
      client_->close();
    if (loop_thread_)
      loop_thread_->stop(true);
  }

  bool SendText(const std::string &msg) {
    std::lock_guard<std::mutex> lk(lock_);
    if (!connected_ || !client_)
      return false;
    return client_->send(msg) >= 0;
  }

  void Poll() {
  }

  bool IsOpen() const {
    std::lock_guard<std::mutex> lk(lock_);
    return connected_;
  }

  void RegisterOpenHandler(tfOpenHandlerCb cb) {
    std::lock_guard<std::mutex> lk(lock_);
    open_handler_ = std::move(cb);
  }

  void RegisterMessageHandler(tfMessageHandlerCb cb) {
    std::lock_guard<std::mutex> lk(lock_);
    message_handler_ = std::move(cb);
  }

  void RegisterCloseHandler(tfCloseHandlerCb cb) {
    std::lock_guard<std::mutex> lk(lock_);
    close_handler_ = std::move(cb);
  }

  void RegisterFailHandler(tfFailHandlerCb cb) {
    std::lock_guard<std::mutex> lk(lock_);
    fail_handler_ = std::move(cb);
  }

private:
  IWebSocketClient::Mode mode_;
  std::shared_ptr<hv::EventLoopThread> loop_thread_;
  std::unique_ptr<hv::WebSocketClient> client_;
  mutable std::mutex lock_;
  bool connected_ = false;
  bool connecting_ = false;
  std::string last_error_;
  tfOpenHandlerCb open_handler_;
  tfMessageHandlerCb message_handler_;
  tfCloseHandlerCb close_handler_;
  tfFailHandlerCb fail_handler_;
};

IWebSocketClient::IWebSocketClient(const Mode &mode) : mode_(mode) {
  impl_ = new Impl(mode_);
}

IWebSocketClient::~IWebSocketClient() {
  Close();
  delete impl_;
  impl_ = nullptr;
}

bool IWebSocketClient::Connect(const std::string &uri) {
  Impl *impl = nullptr;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    impl = impl_;
  }
  // A blocking client runs its event loop here.  Releasing mtx_ lets another
  // thread call Close() to terminate that loop.
  return impl && impl->Connect(uri);
}

void IWebSocketClient::Close(uint16_t code, const std::string &reason) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->Close(code, reason);
}

bool IWebSocketClient::SendText(const std::string &msg) {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ && impl_->SendText(msg);
}

void IWebSocketClient::Poll() {
  if (impl_)
    impl_->Poll();
}

bool IWebSocketClient::IsOpen() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return impl_ && impl_->IsOpen();
}

void IWebSocketClient::RegisterOpenHandler(tfOpenHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterOpenHandler(std::move(cb));
}

void IWebSocketClient::RegisterMessageHandler(tfMessageHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterMessageHandler(std::move(cb));
}

void IWebSocketClient::RegisterCloseHandler(tfCloseHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterCloseHandler(std::move(cb));
}

void IWebSocketClient::RegisterFailHandler(tfFailHandlerCb cb) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (impl_)
    impl_->RegisterFailHandler(std::move(cb));
}
