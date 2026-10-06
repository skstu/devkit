#include "bounded_event_server.h"
#include <App.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <time.h>
#include <vector>
namespace libnet {
int64_t BoundedEventServer::now() {
  timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
  return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
struct BoundedEventServer::Impl {
  struct Client { uint64_t id; };
  using WS = uWS::WebSocket<false, true, Client>;
  struct Ticket { int64_t expires; std::string initial; };
  uWS::App app;
  us_listen_socket_t* listener = nullptr;
  us_timer_t* timer = nullptr;
  std::map<uWS::HttpResponse<false>*, int64_t> pending;
  std::map<uint64_t, WS*> clients;
  std::map<std::string, Ticket> tickets;
  std::string origin, authority, generation;
  int64_t deadline = 0;
  uint64_t next_id = 0;
  int port = 0;
  size_t peak = 0;
  std::function<void()> tick;
  bool stopping = false;
  static constexpr size_t frame_limit = 65536, queue_limit = 131072;
  bool valid() const { return !generation.empty() && now() < deadline; }
  size_t buffered() const {
    size_t total = 0;
    for (auto [id, ws] : clients) total += ws->getBufferedAmount();
    return total;
  }
  bool send(uint64_t id, const std::string& frame) {
    auto it = clients.find(id);
    if (it == clients.end()) return false;
    auto* ws = it->second;
    if (!valid() || frame.size() > frame_limit ||
        ws->getBufferedAmount() + frame.size() + 16 > queue_limit ||
        buffered() + frame.size() + 16 > 524288) {
      ws->close(); return false;
    }
    auto result = ws->send(frame, uWS::OpCode::TEXT, false);
    // A send can synchronously invoke close; never use ws after a dropped send.
    if (result == WS::DROPPED) { if (clients.count(id)) clients.at(id)->close(); return false; }
    peak = std::max(peak, buffered());
    return true;
  }
  void revoke() {
    generation.clear(); deadline = 0; tickets.clear();
    while (!clients.empty()) clients.begin()->second->close();
  }
  void sweep() {
    auto t = now();
    if (!generation.empty() && t >= deadline) revoke();
    for (auto it = tickets.begin(); it != tickets.end();)
      if (t >= it->second.expires) it = tickets.erase(it); else ++it;
    std::vector<uWS::HttpResponse<false>*> expired;
    for (auto [res, limit] : pending) if (t >= limit) expired.push_back(res);
    for (auto* res : expired) if (pending.count(res)) res->close();
  }
  explicit Impl(int requested_port, std::string allowed_origin) : origin(std::move(allowed_origin)) {
    if (origin.rfind("http://127.0.0.1:", 0) != 0) throw std::runtime_error("invalid_origin");
    app.filter([this](auto* res, int change) {
      if (change < 0) { pending.erase(res); return; }
      if (pending.size() >= 4) { res->close(); return; }
      pending.emplace(res, now() + 2000000000);
    });
    uWS::App::WebSocketBehavior<Client> behavior{};
    behavior.compression = uWS::DISABLED;
    behavior.maxPayloadLength = frame_limit;
    behavior.maxBackpressure = queue_limit;
    behavior.closeOnBackpressureLimit = true;
    behavior.idleTimeout = 20;
    behavior.sendPingsAutomatically = true;
    behavior.upgrade = [this](auto* res, auto* req, auto* ctx) {
      sweep();
      // sweep may close the current slow handshake.
      if (!pending.count(res)) return;
      std::map<std::string, std::string> headers;
      bool unique = true; size_t bytes = req->getUrl().size();
      for (auto [k, v] : *req) {
        bytes += k.size() + v.size() + 4;
        if (!headers.emplace(k, v).second) unique = false;
      }
      const std::string prefix = "bridge.events.v1, ticket.";
      auto protocol = headers["sec-websocket-protocol"];
      auto key = protocol.rfind(prefix, 0) == 0 ? protocol.substr(prefix.size()) : "";
      auto ticket = tickets.find(key);
      const auto& wskey = headers["sec-websocket-key"];
      bool key_ok = wskey.size() == 24 && wskey.substr(22) == "==" &&
        std::all_of(wskey.begin(), wskey.begin()+22, [](unsigned char c) {
          return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '+' || c == '/';
        });
      if (!unique || bytes > 4096 || req->getQuery().size() || headers["host"] != authority ||
          headers["origin"] != origin || headers["upgrade"] != "websocket" ||
          headers["connection"].find("Upgrade") == std::string::npos && headers["connection"].find("upgrade") == std::string::npos ||
          headers["sec-websocket-version"] != "13" || !key_ok ||
          headers.count("content-length") || headers.count("transfer-encoding") ||
          !valid() || ticket == tickets.end() || now() >= ticket->second.expires || clients.size() >= 2) {
        res->writeStatus("403 Forbidden")->writeHeader("Connection", "close")->end("denied", true); return;
      }
      auto frame = std::move(ticket->second.initial);
      tickets.erase(ticket); // consume before publishing 101; failure cannot reuse
      auto id = ++next_id;
      pending.erase(res); // upgrade reallocates socket; never retain its old address
      res->template upgrade<Client>({id}, wskey, "bridge.events.v1", "", ctx);
      send(id, frame);
    };
    behavior.open = [this](WS* ws) { clients.emplace(ws->getUserData()->id, ws); };
    behavior.message = [](WS* ws, std::string_view, uWS::OpCode) { ws->close(); }; // read-only, no business frames
    behavior.close = [this](WS* ws, int, std::string_view) { clients.erase(ws->getUserData()->id); };
    app.ws<Client>("/events", std::move(behavior));
    app.any("/*", [](auto* res, auto*) { res->writeStatus("404 Not Found")->end("", true); });
    app.listen("127.0.0.1", requested_port, LIBUS_LISTEN_EXCLUSIVE_PORT, [this](auto* socket) {
      listener = socket;
      if (socket) port = us_socket_local_port(0, reinterpret_cast<us_socket_t*>(socket));
    });
    if (!listener) throw std::runtime_error("listen_failed");
  }
  void stop() {
    if (stopping) return;
    stopping = true; revoke();
    while (!pending.empty()) pending.begin()->first->close();
    if (listener) { us_listen_socket_close(0, listener); listener = nullptr; }
    if (timer) { us_timer_close(timer); timer = nullptr; }
  }
  ~Impl() { stop(); }
};
BoundedEventServer::BoundedEventServer(int port, std::string origin) : impl(new Impl(port, std::move(origin))) {
  impl->authority = "127.0.0.1:" + std::to_string(impl->port);
}
BoundedEventServer::~BoundedEventServer() = default;
int BoundedEventServer::port() const { return impl->port; }
void BoundedEventServer::run(std::function<void()> tick) {
  impl->tick = std::move(tick);
  impl->timer = us_create_timer(reinterpret_cast<us_loop_t*>(uWS::Loop::get()), 0, sizeof(Impl*));
  *static_cast<Impl**>(us_timer_ext(impl->timer)) = impl.get();
  us_timer_set(impl->timer, [](us_timer_t* timer) {
    auto* self = *static_cast<Impl**>(us_timer_ext(timer));
    self->sweep(); self->tick();
  }, 20, 20);
  impl->app.run();
}
void BoundedEventServer::stop() { impl->stop(); }
void BoundedEventServer::revoke() { impl->revoke(); }
bool BoundedEventServer::ticket(std::string key, std::string gen, int64_t deadline, std::string initial) {
  impl->sweep();
  if (key.size() != 43 || gen.empty() || gen.size() > 64 || initial.size() > Impl::frame_limit ||
      deadline <= now() || deadline - now() > int64_t(1800)*1000000000) return false;
  if (gen != impl->generation) { impl->revoke(); impl->generation = std::move(gen); impl->deadline = deadline; }
  else impl->deadline = std::min(impl->deadline, deadline); // never extend a session
  if (impl->tickets.size() >= 4) return false;
  return impl->tickets.emplace(std::move(key), Impl::Ticket{std::min<int64_t>(deadline, now()+10000000000LL), std::move(initial)}).second;
}
bool BoundedEventServer::publish(const std::string& generation, const std::string& frame) {
  impl->sweep();
  if (!impl->valid() || generation != impl->generation || frame.size() > Impl::frame_limit) return false;
  for (auto& [key, ticket] : impl->tickets) ticket.initial = frame;
  std::vector<uint64_t> ids;
  for (auto [id, ws] : impl->clients) ids.push_back(id);
  for (auto id : ids) impl->send(id, frame);
  return true;
}
BoundedEventServer::Stats BoundedEventServer::stats() const {
  return {unsigned(impl->clients.size()), unsigned(impl->pending.size()), unsigned(impl->tickets.size()), impl->buffered(), impl->peak};
}
}
