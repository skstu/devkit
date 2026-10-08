#include "certificate.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <libfix/endpoint.h>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;

struct Application final : FIX::Application {
  std::atomic<unsigned> logons{0}, logouts{0}, messages{0};
  std::atomic<bool> throw_message{false};
  void onCreate(const FIX::SessionID &) override {}
  void onLogon(const FIX::SessionID &) override { ++logons; }
  void onLogout(const FIX::SessionID &) override { ++logouts; }
  void toAdmin(FIX::Message &, const FIX::SessionID &) override {}
  void fromAdmin(const FIX::Message &, const FIX::SessionID &) override {}
  void toApp(FIX::Message &message, const FIX::SessionID &) override {
    if (message.getHeader().isSetField(43) && message.getHeader().getField(43) == "Y")
      throw FIX::DoNotSend();
  }
  void fromApp(const FIX::Message &, const FIX::SessionID &) override {
    if (throw_message)
      throw std::runtime_error("test application failure");
    ++messages;
  }
};
struct Events {
  std::atomic<unsigned> count{0};
  std::mutex mutex;
  std::string reason;
  void record(const libfix::Event &event) {
    std::lock_guard lock(mutex);
    reason = event.reason;
    ++count;
  }
  std::string last() {
    std::lock_guard lock(mutex);
    return reason;
  }
};
template <class F> void await(F condition) {
  const auto end = std::chrono::steady_clock::now() + 4s;
  while (!condition() && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(5ms);
  CHECK(condition());
}
libfix::SessionConfig session(const std::string &sender, const std::string &target) {
  FIX::Dictionary settings;
  for (const auto &[key, value] : std::map<std::string, std::string>{{"StartTime", "00:00:00"},
                                                                     {"EndTime", "00:00:00"},
                                                                     {"NonStopSession", "Y"},
                                                                     {"HeartBtInt", "1"},
                                                                     {"UseDataDictionary", "N"},
                                                                     {"PersistMessages", "Y"},
                                                                     {"ResetOnLogon", "N"},
                                                                     {"ResetOnLogout", "N"},
                                                                     {"ResetOnDisconnect", "N"}})
    settings.setString(key, value);
  return {FIX::SessionID("FIX.4.4", sender, target), std::move(settings)};
}
libfix::Options options(const Certificate &certificate, bool server, std::string name,
                        uint16_t port = 0) {
  libfix::Options o;
  o.role = server ? libfix::Role::acceptor : libfix::Role::initiator;
  o.port = port;
  o.certificate = certificate.cert.string();
  o.private_key = certificate.key.string();
  o.certificate_authorities = certificate.cert.string();
  o.store_directory = (certificate.folder / name).string();
  o.sessions.push_back(server ? session("SERVER", name) : session(name, "SERVER"));
  o.limits.session_tick_ms = 20;
  return o;
}
void send(libfix::Endpoint &endpoint, const FIX::SessionID &id, std::string text = "test") {
  endpoint.call([&] {
    FIX::Message m;
    m.getHeader().setField(35, "U1");
    m.setField(58, text);
    CHECK(endpoint.session(id).send(m));
  });
}
void reconnect_and_isolation(Certificate &certificate) {
  Application sa, ca, other_app;
  Events events;
  auto so = options(certificate, true, "CLIENT");
  so.store_directory += "-server";
  so.sessions.push_back(session("SERVER", "OTHER"));
  auto server = std::make_unique<libfix::Endpoint>(sa, so);
  server->start();
  auto co = options(certificate, false, "CLIENT", server->port());
  co.reconnect_min_ms = 20;
  co.reconnect_max_ms = 80;
  libfix::Endpoint client(ca, co, {[&](const auto &e) { events.record(e); }, {}});
  client.start();
  const auto id = co.sessions.front().id;
  await([&] { return ca.logons == 1 && sa.logons == 1; });
  send(client, id);
  await([&] { return sa.messages == 1; });
  const auto next_before = client.call([&] { return client.session(id).getExpectedSenderNum(); });
  bool leased = false;
  try {
    libfix::Endpoint duplicate(ca, co);
    duplicate.start();
  } catch (const std::exception &e) {
    leased = std::string(e.what()).find("already in use") != std::string::npos;
  }
  CHECK(leased);
  auto oo = options(certificate, false, "OTHER", server->port());
  libfix::Endpoint other(other_app, oo);
  other.start();
  await([&] { return other_app.logons == 1; });
  server->call([&] { server->disconnect(so.sessions.front().id, "test reconnect"); });
  await([&] { return ca.logons >= 2; });
  CHECK(client.call([&] { return client.session(id).getExpectedSenderNum(); }) > next_before);
  CHECK(other_app.logouts == 0);
  send(other, oo.sessions.front().id);
  send(client, id);
  await([&] { return sa.messages == 3; });
  CHECK(client.metrics().reconnects >= 1);
  // Restart the acceptor with the same stores: counters survive, no business
  // message is replayed, and another successful send still reaches its owner.
  server.reset();
  so.port = co.port;
  server = std::make_unique<libfix::Endpoint>(sa, so);
  server->start();
  await([&] { return ca.logons >= 3; });
  CHECK(sa.messages == 3);
  send(client, id);
  await([&] { return sa.messages == 4; });
  CHECK(events.count >= 2);
}
void identity_and_callback_failure(Certificate &certificate) {
  Application sa, ca;
  Events rejected, closed;
  auto so = options(certificate, true, "AUTH");
  so.store_directory += "-server";
  libfix::Endpoint server(sa, so);
  server.start();
  auto co = options(certificate, false, "AUTH", server.port());
  co.peer_name = "127.0.0.2";
  {
    libfix::Endpoint wrong(ca, co, {[&](const auto &e) { rejected.record(e); }, {}});
    wrong.start();
    await([&] { return rejected.count > 0; });
    CHECK(ca.logons == 0 && sa.logons == 0);
  }
  co.peer_name.clear();
  co.stores = std::make_shared<FIX::MemoryStoreFactory>();
  co.store_directory.clear(); // Custom store policy remains injectable.
  libfix::Endpoint client(ca, co, {[&](const auto &e) { closed.record(e); }, {}});
  client.start();
  await([&] { return ca.logons == 1; });
  sa.throw_message = true;
  send(client, co.sessions.front().id);
  await([&] { return closed.count > 0; });
  CHECK(sa.messages == 0);
}
void bounds_and_shutdown(Certificate &certificate) {
  Application sa, ca;
  Events closed;
  auto so = options(certificate, true, "BOUNDS");
  so.store_directory += "-server";
  libfix::Endpoint server(sa, so);
  server.start();
  auto co = options(certificate, false, "BOUNDS", server.port());
  co.limits.frame_bytes = 1024;
  co.limits.peer_queue_bytes = 2048;
  co.limits.total_queue_bytes = 4096;
  co.limits.pending_tasks = 2;
  auto client = std::make_unique<libfix::Endpoint>(
      ca, co, libfix::Callbacks{[&](const auto &e) { closed.record(e); }, {}});
  client->start();
  await([&] { return ca.logons == 1; });
  // Hold one callback to exercise actual producer saturation, then release it.
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  CHECK(client->post([&] {
    entered.set_value();
    gate.wait();
  }));
  entered.get_future().wait();
  CHECK(client->post([] {}));
  CHECK(client->post([] {}));
  CHECK(!client->post([] {}));
  release.set_value();
  CHECK(client->metrics().rejected_tasks == 1);
  client->call([&] {
    auto &s = client->session(co.sessions.front().id);
    for (int i = 0; i < 30; ++i) {
      FIX::Message m;
      m.getHeader().setField(35, "U1");
      m.setField(58, std::string(700, 'x'));
      if (!s.send(m))
        break;
    }
  });
  await([&] { return closed.count > 0; });
  CHECK(closed.last().find("queue budget") != std::string::npos);
  CHECK(client->metrics().peak_queued_bytes <= co.limits.peer_queue_bytes);
  const auto before = std::chrono::steady_clock::now();
  client->stop();
  client->stop();
  CHECK(!client->post([] {}));
  client.reset();
  CHECK(std::chrono::steady_clock::now() - before < 1s);
  // Shutdown while connect/handshake is still pending must join and drain all
  // native callbacks without touching a destroyed application or store.
  co.store_directory += "-pending";
  libfix::Endpoint pending(ca, co);
  pending.start();
}
int main() {
#ifndef _WIN32
  std::signal(SIGPIPE, SIG_IGN);
#endif
  try {
    Certificate certificate;
    reconnect_and_isolation(certificate);
    identity_and_callback_failure(certificate);
    bounds_and_shutdown(certificate);
    std::cout << "libfix TLS, reconnect, persisted sequences, isolation, backpressure and shutdown "
                 "passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
