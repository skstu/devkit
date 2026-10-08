// Extracted and generalized from tdbrg node_client.cpp, tls_node.hpp and node_fix.hpp.
// Transport ownership, framing, TLS and session lifecycle unified for devkit.
#include <libfix/endpoint.h>
#include <libnet_uv.h>
#include <quickfix/FileStore.h>
#include <quickfix/SessionFactory.h>
#ifndef QUICKFIX_NONSTOP_SESSION_CONSTRUCTOR
#error "libfix requires devkit quickfix 1.16.0 port revision 3 (NonStopSession constructor fix)"
#endif
#include <algorithm>
#include <atomic>
#include <charconv>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <openssl/ssl.h>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace libfix {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
class Lease {
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
public:
  void open(const std::string &directory) {
    require(std::filesystem::path(directory).is_absolute(), "absolute FIX store path required");
    std::filesystem::create_directories(directory);
#ifdef _WIN32
    auto path = (std::filesystem::path(directory) / ".lock").wstring();
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle_ != INVALID_HANDLE_VALUE, "FIX store already in use");
#else
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    fd_ = ::open((directory + "/.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd_ >= 0 && !flock(fd_, LOCK_EX | LOCK_NB), "FIX store already in use");
#endif
  }
  ~Lease() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
#else
    if (fd_ >= 0)
      ::close(fd_);
#endif
  }
};
} // namespace

struct Endpoint::Impl {
  struct Peer;
  struct Slot {
    FIX::Session *session = nullptr;
    Peer *peer = nullptr;
  };
  struct Peer final : FIX::Responder {
    Impl &owner;
    libnet::TcpClient socket;
    SSL *ssl = nullptr;
    Slot *slot = nullptr;
    std::string input, reason;
    uint64_t since = 0, active = 0;
    size_t queued = 0;
    bool ready = false, closing = false, reaped = false;
    explicit Peer(Impl &parent) : owner(parent) {}
    ~Peer() { SSL_free(ssl); }
    bool send(const std::string &bytes) override {
      if (closing || !ready)
        return false;
      try {
        return owner.send(*this, bytes);
      } catch (...) {
        owner.close(*this, "TLS send failed");
        return false;
      }
    }
    void disconnect() override { owner.close(*this, "FIX session disconnected"); }
  };
  FIX::Application &application;
  Options options;
  Callbacks callbacks;
  Lease lease;
  std::unique_ptr<FIX::SessionFactory> factory;
  std::map<std::string, Slot> slots;
  std::unordered_map<Peer *, std::unique_ptr<Peer>> peers;
  SSL_CTX *context = nullptr;
  uv_loop_t loop{};
  uv_async_t wake{};
  uv_timer_t timer{};
  libnet::TcpServer listener;
  libnet::Resolver resolver;
  std::thread worker;
  std::mutex mutex;
  std::deque<std::function<void()>> tasks;
  bool accepting = true, stopping = false, started = false;
  bool waking = false, timing = false;
  bool resolving = false;
  uint64_t connect_since = 0, next_retry = 0, retry_delay = 0, next_session = 0,
           next_application = 0;
  Metrics counters;
  std::atomic<uint64_t> rejected{0};

  Impl(FIX::Application &app, Options opts, Callbacks cb)
      : application(app), options(std::move(opts)), callbacks(std::move(cb)) {
    const auto &l = options.limits;
    require(!options.sessions.empty(), "FIX sessions required");
    require(l.connections && l.frame_bytes >= 64 && l.frame_bytes <= 16 * 1024 * 1024 &&
                l.peer_queue_bytes >= l.frame_bytes && l.total_queue_bytes >= l.peer_queue_bytes &&
                l.pending_tasks && l.connect_timeout_ms && l.handshake_timeout_ms &&
                l.session_tick_ms,
            "invalid FIX runtime limits");
    require(options.role == Role::acceptor || (options.sessions.size() == 1 && options.port),
            "initiator requires one session and a port");
    require(!options.reconnect_min_ms || options.reconnect_max_ms >= options.reconnect_min_ms,
            "invalid reconnect bounds");
    require(!options.host.empty(), "FIX endpoint host required");
    require(uv_loop_init(&loop) == 0, "FIX loop initialization failed");
    wake.data = timer.data = this;
    try {
      require(uv_async_init(&loop, &wake,
                            [](uv_async_t *h) { static_cast<Impl *>(h->data)->drain(); }) == 0,
              "FIX wake initialization failed");
      waking = true;
      require(uv_timer_init(&loop, &timer) == 0, "FIX timer initialization failed");
      timing = true;
      worker = std::thread([this] { uv_run(&loop, UV_RUN_DEFAULT); });
    } catch (...) {
      if (timing)
        uv_close(reinterpret_cast<uv_handle_t *>(&timer), nullptr);
      if (waking)
        uv_close(reinterpret_cast<uv_handle_t *>(&wake), nullptr);
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
      throw;
    }
  }
  bool on_loop() const { return worker.get_id() == std::this_thread::get_id(); }
  bool enqueue(std::function<void()> task, bool control) {
    std::lock_guard lock(mutex);
    if (!accepting || tasks.size() >= options.limits.pending_tasks + (control ? 16 : 0)) {
      ++rejected;
      return false;
    }
    tasks.push_back(std::move(task));
    uv_async_send(&wake);
    return true;
  }
  void event(const std::string &session, const std::string &reason) noexcept {
    try {
      if (callbacks.disconnected && !stopping)
        callbacks.disconnected({session, reason});
    } catch (...) {
    }
  }
  void close(Peer &p, std::string reason) {
    if (p.closing)
      return;
    p.closing = true;
    p.reason = std::move(reason);
    uv_async_send(&wake); // Leave QuickFIX's stack before detaching its responder.
  }
  void fail_all(const char *reason) noexcept {
    try {
      for (auto &[key, p] : peers)
        close(*p, reason);
    } catch (...) {
    }
  }
  void retry() {
    if (stopping || !options.reconnect_min_ms)
      return;
    retry_delay = retry_delay ? std::min(options.reconnect_max_ms, retry_delay * 2)
                              : options.reconnect_min_ms;
    next_retry = uv_now(&loop) + retry_delay;
  }
  void reap() {
    for (auto &[key, ptr] : peers) {
      auto &p = *ptr;
      if (!p.closing || p.reaped)
        continue;
      p.reaped = true;
      std::string id;
      if (p.slot) {
        auto *slot = p.slot;
        id = slot->session->getSessionID().toString();
        p.slot = nullptr;
        slot->peer = nullptr;
        slot->session->setResponder(nullptr);
        try {
          slot->session->disconnect();
        } catch (...) {
        }
      }
      event(id, p.reason);
      p.socket.Close([this, key](int) { peers.erase(key); });
      if (options.role == Role::initiator)
        retry();
    }
  }
  void drain() noexcept {
    std::deque<std::function<void()>> work;
    {
      std::lock_guard lock(mutex);
      work.swap(tasks);
    }
    for (auto &task : work) {
      try {
        task();
      } catch (...) {
        fail_all("FIX application task failed");
      }
      reap();
    }
    reap();
    if (stopping) {
      resolver.Cancel();
      listener.Close();
      for (auto &[id, slot] : slots)
        if (slot.session) {
          slot.session->setResponder(nullptr);
          factory->destroy(slot.session);
          slot.session = nullptr;
        }
      uv_timer_stop(&timer);
      if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&timer)))
        uv_close(reinterpret_cast<uv_handle_t *>(&timer), nullptr);
      if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&wake)))
        uv_close(reinterpret_cast<uv_handle_t *>(&wake), nullptr);
    }
  }
  void setup() {
    if (!options.store_directory.empty())
      lease.open(options.store_directory);
    if (!options.stores) {
      require(!options.store_directory.empty(), "FIX FileStore directory required");
      options.stores = std::make_shared<FIX::FileStoreFactory>(options.store_directory);
    }
    for (const auto &[name, bytes] : options.dictionaries) {
      require(!options.store_directory.empty() && !name.empty() && name != "." && name != ".." &&
                  name.find_first_of("/\\") == name.npos,
              "invalid FIX dictionary filename");
      const auto path = std::filesystem::path(options.store_directory) / name;
      require(!std::filesystem::is_symlink(path), "FIX dictionary must not be a symlink");
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file << bytes;
      require(bool(file), "FIX dictionary write failed");
    }
    factory =
        std::make_unique<FIX::SessionFactory>(application, *options.stores, options.logs.get());
    for (auto config : options.sessions) {
      const auto name = config.id.toString();
      require(!slots.contains(name), "duplicate FIX session");
      config.settings.setString("ConnectionType",
                                options.role == Role::initiator ? "initiator" : "acceptor");
      auto [it, inserted] = slots.try_emplace(name);
      (void)inserted;
      it->second.session = factory->create(config.id, config.settings);
    }
    context =
        SSL_CTX_new(options.role == Role::initiator ? TLS_client_method() : TLS_server_method());
    require(context, "TLS context failed");
    require(SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) == 1,
            "TLS version setup failed");
    SSL_CTX_set_options(context, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_max_early_data(context, 0);
    if (options.role == Role::initiator) {
      SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
      require(options.certificate_authorities.empty()
                  ? SSL_CTX_set_default_verify_paths(context) == 1
                  : SSL_CTX_load_verify_locations(context, options.certificate_authorities.c_str(),
                                                  nullptr) == 1,
              "TLS trust unavailable");
    } else {
      require(SSL_CTX_use_certificate_chain_file(context, options.certificate.c_str()) == 1 &&
                  SSL_CTX_use_PrivateKey_file(context, options.private_key.c_str(),
                                              SSL_FILETYPE_PEM) == 1 &&
                  SSL_CTX_check_private_key(context) == 1,
              "TLS certificate/key invalid");
    }
  }
  Peer &make_peer() {
    auto p = std::make_unique<Peer>(*this);
    require(p->socket.Initialize(&loop), "TCP initialization failed");
    auto *key = p.get();
    peers.emplace(key, std::move(p));
    key->since = key->active = uv_now(&loop);
    ++counters.connections;
    return *key;
  }
  bool flush(Peer &p) {
    const auto n = size_t(BIO_ctrl_pending(SSL_get_wbio(p.ssl)));
    if (!n)
      return true;
    const auto &l = options.limits;
    if (n > l.peer_queue_bytes || p.queued > l.peer_queue_bytes - n || n > l.total_queue_bytes ||
        counters.queued_bytes > l.total_queue_bytes - n) {
      close(p, "TLS outbound queue budget");
      return false;
    }
    std::string bytes(n, '\0');
    require(BIO_read(SSL_get_wbio(p.ssl), bytes.data(), int(n)) == int(n), "TLS output failed");
    p.queued += n;
    counters.queued_bytes += n;
    counters.peak_queued_bytes = std::max(counters.peak_queued_bytes, counters.queued_bytes);
    if (!p.socket.Send(bytes, [this, peer = &p, n](int status) {
          peer->queued -= n;
          counters.queued_bytes -= n;
          if (status)
            close(*peer, "TCP write failed");
        })) {
      p.queued -= n;
      counters.queued_bytes -= n;
      close(p, "TCP write admission failed");
      return false;
    }
    counters.sent_bytes += n;
    return true;
  }
  bool send(Peer &p, const std::string &bytes) {
    require(bytes.size() <= options.limits.frame_bytes, "FIX send frame budget");
    // Memory BIO TLS never blocks waiting for the socket. uv_write owns ciphertext.
    require(SSL_write(p.ssl, bytes.data(), int(bytes.size())) == int(bytes.size()),
            "TLS write failed");
    return flush(p);
  }
  void frames(Peer &p, const char *bytes, size_t count) {
    const auto limit = options.limits.frame_bytes;
    require(p.input.size() + count <= limit + 8192, "FIX receive buffer budget");
    p.input.append(bytes, count);
    while (!p.closing && !p.input.empty()) {
      const auto &b = p.input;
      if (b.size() < 2)
        return;
      require(b.starts_with("8="), "FIX BeginString required");
      const auto first = b.find('\1');
      if (first == b.npos) {
        require(b.size() < 64, "invalid FIX header");
        return;
      }
      if (b.size() < first + 4)
        return;
      require(b.compare(first + 1, 2, "9=") == 0, "FIX BodyLength must be second");
      const auto second = b.find('\1', first + 1);
      if (second == b.npos) {
        require(b.size() < 80, "invalid FIX BodyLength");
        return;
      }
      size_t body = 0;
      const auto result = std::from_chars(b.data() + first + 3, b.data() + second, body);
      require(result.ec == std::errc{} && result.ptr == b.data() + second && body <= limit,
              "invalid FIX BodyLength");
      const size_t length = second + 1 + body + 7;
      require(length <= limit, "FIX frame budget");
      if (b.size() < length)
        return;
      require(b.compare(length - 7, 3, "10=") == 0 && b[length - 1] == '\1',
              "invalid FIX checksum framing");
      auto raw = b.substr(0, length);
      p.input.erase(0, length);
      if (!p.slot) {
        FIX::Message first_message(raw, true);
        const auto &header = first_message.getHeader();
        require(header.getField(35) == "A", "FIX Logon required");
        const auto id =
            FIX::SessionID(header.getField(8), header.getField(56), header.getField(49));
        auto it = slots.find(id.toString());
        require(it != slots.end() && !it->second.peer, "unconfigured or connected FIX session");
        p.slot = &it->second;
        p.slot->peer = &p;
        p.slot->session->setResponder(&p);
      }
      ++counters.received_messages;
      p.slot->session->next(raw, FIX::UtcTimeStamp::now());
      if (p.slot->session->isLoggedOn())
        retry_delay = 0;
    }
  }
  void read(Peer &p) {
    if (!p.ready) {
      const int status = SSL_do_handshake(p.ssl);
      if (status == 1) {
        require(options.role == Role::acceptor || SSL_get_verify_result(p.ssl) == X509_V_OK,
                "TLS peer identity rejected");
        p.ready = true;
        if (options.role == Role::initiator) {
          p.slot = &slots.begin()->second;
          p.slot->peer = &p;
          p.slot->session->setResponder(&p);
        }
      } else {
        const auto error = SSL_get_error(p.ssl, status);
        require(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE,
                "TLS handshake failed");
      }
      if (!flush(p) || !p.ready)
        return;
    }
    char plaintext[8192];
    // A TCP callback contributes at most one bounded libuv read. TLS records
    // are drained so SSL_pending never waits for a new socket edge.
    while (!p.closing) {
      const auto n = SSL_read(p.ssl, plaintext, sizeof(plaintext));
      if (n <= 0) {
        const auto error = SSL_get_error(p.ssl, n);
        require(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE,
                "TLS peer closed or read failed");
        flush(p);
        return;
      }
      p.active = uv_now(&loop);
      frames(p, plaintext, size_t(n));
    }
  }
  void attach(Peer &p) {
    p.since = p.active = uv_now(&loop);
    require(uv_tcp_nodelay(p.socket.native_handle(), 1) == 0, "TCP nodelay failed");
    p.ssl = SSL_new(context);
    auto *in = BIO_new(BIO_s_mem());
    auto *out = BIO_new(BIO_s_mem());
    if (!p.ssl || !in || !out) {
      BIO_free(in);
      BIO_free(out);
      throw std::runtime_error("TLS memory BIO failed");
    }
    SSL_set_bio(p.ssl, in, out);
    if (options.role == Role::acceptor)
      SSL_set_accept_state(p.ssl);
    else {
      SSL_set_connect_state(p.ssl);
      const auto name = options.peer_name.empty() ? options.host : options.peer_name;
      if (libnet::Endpoint::Parse(name, options.port))
        require(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(p.ssl), name.c_str()) == 1,
                "TLS IP identity setup failed");
      else {
        require(SSL_set1_host(p.ssl, name.c_str()) == 1 &&
                    SSL_set_tlsext_host_name(p.ssl, name.c_str()) == 1,
                "TLS DNS identity setup failed");
      }
    }
    require(p.socket.StartRead(
                [this, peer = &p](std::string_view bytes) {
                  if (peer->closing)
                    return;
                  try {
                    counters.received_bytes += bytes.size();
                    require(bytes.size() <= 1024 * 1024 &&
                                BIO_write(SSL_get_rbio(peer->ssl), bytes.data(),
                                          int(bytes.size())) == int(bytes.size()),
                            "TLS input budget");
                    read(*peer);
                  } catch (const std::exception &e) {
                    close(*peer, e.what());
                  } catch (...) {
                    close(*peer, "FIX receive callback failed");
                  }
                  reap();
                },
                [this, peer = &p](int) {
                  close(*peer, "TCP peer closed");
                  reap();
                }),
            "TCP read start failed");
    read(p);
  }
  void connected(Peer &p, int status) noexcept {
    if (p.closing || stopping)
      return;
    try {
      require(status == 0, "TCP connect failed");
      attach(p);
    } catch (const std::exception &e) {
      close(p, e.what());
    } catch (...) {
      close(p, "TLS setup failed");
    }
    reap();
  }
  void dial(const libnet::Endpoint &address) {
    auto &p = make_peer();
    if (!p.socket.Connect(address, [this, peer = &p](int status) { connected(*peer, status); }))
      close(p, "TCP connect start failed");
  }
  void connect() {
    next_retry = 0;
    connect_since = uv_now(&loop);
    if (auto address = libnet::Endpoint::Parse(options.host, options.port)) {
      dial(*address);
      return;
    }
    resolving = true;
    if (!resolver.Resolve(&loop, options.host, options.port,
                          [this](int status, std::vector<libnet::Endpoint> addresses) {
                            resolving = false;
                            if (stopping)
                              return;
                            try {
                              if (status || addresses.empty()) {
                                event({}, "DNS lookup failed");
                                retry();
                                return;
                              }
                              dial(addresses.front());
                            } catch (...) {
                              event({}, "TCP connection setup failed");
                              retry();
                            }
                          })) {
      resolving = false;
      event({}, "DNS lookup admission failed");
      retry();
    }
  }
  void tick() noexcept {
    try {
      const auto now = uv_now(&loop);
      const auto &l = options.limits;
      if (resolving && now - connect_since > l.connect_timeout_ms)
        resolver.Cancel();
      if (!resolving && peers.empty() && next_retry && now >= next_retry) {
        ++counters.reconnects;
        connect();
      }
      for (auto &[key, p] : peers) {
        if (p->closing)
          continue;
        if (!p->ssl && now - p->since > l.connect_timeout_ms)
          close(*p, "TCP connect timeout");
        else if (p->ssl && !p->ready && now - p->since > l.handshake_timeout_ms)
          close(*p, "TLS handshake timeout");
        else if (l.inbound_idle_ms && now - p->active > l.inbound_idle_ms)
          close(*p, "FIX inbound idle timeout");
      }
      if (now >= next_session) {
        next_session = now + l.session_tick_ms;
        for (auto &[id, slot] : slots) {
          if (!slot.peer || slot.peer->closing)
            continue;
          try {
            slot.session->next(FIX::UtcTimeStamp::now());
          } catch (...) {
            close(*slot.peer, "FIX session timer failed");
          }
        }
      }
      reap();
      if (options.application_tick_ms && now >= next_application) {
        next_application = now + options.application_tick_ms;
        if (callbacks.tick)
          callbacks.tick();
      }
    } catch (...) {
      fail_all("FIX reactor timer failed");
    }
    reap();
  }
  void start() {
    require(!started, "FIX endpoint already started");
    started = true;
    setup();
    if (options.role == Role::acceptor) {
      require(listener.Listen(
                  &loop, options.host, options.port,
                  [this](int status) {
                    if (status || stopping)
                      return;
                    Peer *p = nullptr;
                    try {
                      p = &make_peer();
                      require(p->socket.Accept(listener.native_handle()), "TCP accept failed");
                      require(peers.size() <= options.limits.connections, "FIX connection budget");
                      attach(*p);
                    } catch (const std::exception &e) {
                      if (p)
                        close(*p, e.what());
                    } catch (...) {
                      if (p)
                        close(*p, "TLS accept failed");
                    }
                    reap();
                  }),
              "TLS listen failed");
    } else
      connect();
    const auto interval = options.application_tick_ms ? std::min(options.application_tick_ms,
                                                                 options.limits.session_tick_ms)
                                                      : options.limits.session_tick_ms;
    require(uv_timer_start(
                &timer, [](uv_timer_t *t) { static_cast<Impl *>(t->data)->tick(); }, 0, interval) ==
                0,
            "FIX timer start failed");
  }
  void stop() {
    require(!on_loop(), "stop FIX endpoint from its owner thread");
    if (!worker.joinable())
      return;
    {
      std::lock_guard lock(mutex);
      accepting = false;
      // Shutdown cannot be rejected by an already-full application queue.
      tasks.push_back([this] {
        stopping = true;
        fail_all("endpoint stopping");
      });
      uv_async_send(&wake);
    }
    worker.join();
  }
  ~Impl() {
    stop();
    SSL_CTX_free(context);
    if (uv_loop_close(&loop) != 0)
      std::terminate();
  }
};

Endpoint::Endpoint(FIX::Application &application, Options options, Callbacks callbacks)
    : impl_(std::make_unique<Impl>(application, std::move(options), std::move(callbacks))) {}
Endpoint::~Endpoint() = default;
bool Endpoint::on_loop() const { return impl_->on_loop(); }
bool Endpoint::enqueue(std::function<void()> work, bool control) {
  return impl_->enqueue(std::move(work), control);
}
bool Endpoint::post(std::function<void()> work) { return enqueue(std::move(work), false); }
void Endpoint::start() {
  call([this] { impl_->start(); });
}
void Endpoint::stop() { impl_->stop(); }
FIX::Session &Endpoint::session(const FIX::SessionID &id) {
  require(on_loop(), "FIX session access requires the reactor thread");
  auto it = impl_->slots.find(id.toString());
  require(it != impl_->slots.end() && it->second.session, "unconfigured FIX session");
  return *it->second.session;
}
void Endpoint::disconnect(const FIX::SessionID &id, std::string reason) {
  require(on_loop(), "FIX disconnect requires the reactor thread");
  auto it = impl_->slots.find(id.toString());
  if (it != impl_->slots.end() && it->second.peer)
    impl_->close(*it->second.peer, std::move(reason));
}
uint16_t Endpoint::port() {
  return call([this] {
    return impl_->options.role == Role::acceptor ? impl_->listener.Port() : impl_->options.port;
  });
}
Metrics Endpoint::metrics() {
  return call([this] {
    auto result = impl_->counters;
    result.rejected_tasks = impl_->rejected.load();
    return result;
  });
}
} // namespace libfix
