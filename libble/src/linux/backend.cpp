#include "../common/backend.hpp"
#include "../common/command_queue.hpp"
#include "../common/read_session.hpp"
#include <cerrno>
#include <sys/random.h>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <deque>
#include <gio/gio.h>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
namespace dkble {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
struct Variant {
  GVariant *p = nullptr;
  Variant() = default;
  explicit Variant(GVariant *v) : p(v) {}
  Variant(const Variant &v) : p(v.p ? g_variant_ref(v.p) : nullptr) {}
  Variant(Variant &&v) noexcept : p(std::exchange(v.p, nullptr)) {}
  Variant &operator=(Variant v) {
    std::swap(p, v.p);
    return *this;
  }
  ~Variant() {
    if (p)
      g_variant_unref(p);
  }
};
std::string string(GVariant *p, const char *key) {
  const char *out = nullptr;
  if (p)
    g_variant_lookup(p, key, "&s", &out);
  return out ? out : "";
}
std::string object(GVariant *p, const char *key) {
  const char *out = nullptr;
  if (p)
    g_variant_lookup(p, key, "&o", &out);
  return out ? out : "";
}
bool boolean(GVariant *p, const char *key) {
  gboolean out = false;
  if (p)
    g_variant_lookup(p, key, "b", &out);
  return out;
}
uint16_t number(GVariant *p, const char *key, uint16_t value) {
  if (p)
    g_variant_lookup(p, key, "q", &value);
  return value;
}
bool contains(GVariant *p, const char *key, const std::string &value) {
  Variant values(p ? g_variant_lookup_value(p, key, G_VARIANT_TYPE("as"))
                   : nullptr);
  if (!values.p)
    return false;
  GVariantIter iter;
  g_variant_iter_init(&iter, values.p);
  const char *s;
  while (g_variant_iter_next(&iter, "&s", &s))
    if (value == s)
      return true;
  return false;
}
GVariant *dictionary() {
  return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
}
std::vector<uint8_t> bytes(GVariant *p) {
  if (!p)
    return {};
  gsize size = 0;
  const auto *data =
      static_cast<const uint8_t *>(g_variant_get_fixed_array(p, &size, 1));
  if (!data || size > 512)
    return {};
  return {data, data + size};
}
const char *xml = R"(<node>
<interface name="org.freedesktop.DBus.ObjectManager"><method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method></interface>
<interface name="org.bluez.GattService1"><property name="UUID" type="s" access="read"/><property name="Primary" type="b" access="read"/><property name="Includes" type="ao" access="read"/></interface>
<interface name="org.bluez.GattCharacteristic1">
<method name="WriteValue"><arg type="ay" direction="in"/><arg type="a{sv}" direction="in"/></method>
<method name="ReadValue"><arg type="a{sv}" direction="in"/><arg type="ay" direction="out"/></method>
<property name="UUID" type="s" access="read"/><property name="Service" type="o" access="read"/><property name="Flags" type="as" access="read"/>
</interface>
<interface name="org.bluez.LEAdvertisement1"><method name="Release"/><property name="Type" type="s" access="read"/><property name="ServiceUUIDs" type="as" access="read"/></interface>
</node>)";
struct Bluez : std::enable_shared_from_this<Bluez> {
  struct Link {
    std::string id, peer, probe, rx, tx;
    bool initiator = false, ready = false, owned = false;
    bool polling = false, reading = false, helloSent = false;
    read_session::Epoch epoch{};
    Clock::time_point readAt{}, writeAt{}, resolveAt{};
    size_t mtu = 20, offset = 0;
    size_t readCount = 0, writeCount = 0;
    std::vector<uint8_t> pending;
    bool sending = false, writing = false;
    uint64_t request = 0;
    Clock::time_point deadline = Clock::now() + 20s;
    std::deque<std::vector<uint8_t>> early;
    size_t earlyBytes = 0;
  };
  Config config, pendingConfig;
  bool configDirty = false;
  Emit emit;
  std::mutex mutex;
  CommandQueue commands;
  std::condition_variable stoppedCondition;
  uint64_t completedStop = 0;
  int stopStatus = DKBLE_OK;
  bool closing = false;
  GMainContext *context = g_main_context_new();
  GMainLoop *loop = g_main_loop_new(context, false);
  GDBusConnection *bus = nullptr;
  GCancellable *cancel = nullptr;
  GDBusNodeInfo *info = nullptr;
  std::thread worker;
  uint64_t generation = 0, serial = 0;
  size_t calls = 0;
  std::string adapter, bluezOwner, mode = "off";
  const std::string root = "/com/skstu/devkit/ble",
                    service = root + "/service0", rx = service + "/rx",
                    tx = service + "/tx", advert = root + "/advertisement";
  std::vector<guint> registered;
  guint properties = 0, added = 0, removed = 0;
  using Interfaces = std::map<std::string, Variant>;
  std::map<std::string, Interfaces> objects;
  std::map<std::string, std::shared_ptr<Link>> links;
  std::map<std::string, Clock::time_point> candidates, retries;
  std::set<std::string> wanted;
  read_session::Registry sessions;
  explicit Bluez(Config c, Emit e)
      : config(c), pendingConfig(std::move(c)), emit(std::move(e)) {}
  ~Bluez() {
    g_main_loop_unref(loop);
    g_main_context_unref(context);
    if (info)
      g_dbus_node_info_unref(info);
  }
  void start() {
    auto self = shared_from_this();
    worker = std::thread([self] { self->run(); });
  }
  void event(uint32_t type, std::string detail = {}, int status = 0) {
    Event e;
    e.type = type;
    e.detail = std::move(detail);
    e.status = status;
    e.generation = generation;
    emit(std::move(e));
  }
  void linkEvent(uint32_t type, const Link &l, std::vector<uint8_t> data = {}) {
    Event e;
    e.type = type;
    e.generation = generation;
    e.link = l.id;
    e.peer = l.peer;
    e.probe = l.probe;
    e.initiator = l.initiator;
    e.data = std::move(data);
    emit(std::move(e));
  }
  void failure(std::string stage, int status = DKBLE_IO) {
    event(DKBLE_EVENT_ERROR, std::move(stage), status);
  }
  void run() {
    g_main_context_push_thread_default(context);
    auto source = g_timeout_source_new(20);
    g_source_set_callback(
        source,
        [](gpointer p) -> gboolean {
          auto *s = static_cast<Bluez *>(p);
          s->tick();
          return G_SOURCE_CONTINUE;
        },
        this, nullptr);
    g_source_attach(source, context);
    g_main_loop_run(loop);
    g_source_destroy(source);
    g_source_unref(source);
    stop();
    // Cancelled async replies own their callback allocations until dispatched.
    while (calls)
      g_main_context_iteration(context, true);
    g_main_context_pop_thread_default(context);
  }
  void close() {
    {
      std::lock_guard lock(mutex);
      closing = true;
      commands.clear();
      stoppedCondition.notify_all();
    }
    g_main_context_wakeup(context);
    if (worker.joinable())
      worker.join();
  }
  int command(Command c) {
    std::unique_lock lock(mutex);
    if (closing)
      return DKBLE_STATE;
    if (c.op == Op::stop) {
      const auto ticket = commands.request_stop(c.generation);
      g_main_context_wakeup(context);
      // Four owned disconnects are bounded to 1s each. A stuck worker must
      // return failure, never a fabricated off state; destroy still joins it.
      if (!stoppedCondition.wait_for(lock, 10s, [&] {
            return completedStop >= ticket || closing;
          }))
        return DKBLE_IO;
      return closing ? DKBLE_STATE : stopStatus;
    }
    const auto status = commands.push(std::move(c));
    if (!status)
      g_main_context_wakeup(context);
    return status;
  }
  using Callback = std::function<void(GVariant *, GError *)>;
  struct Pending {
    std::weak_ptr<Bluez> self;
    uint64_t generation;
    Callback fn;
  };
  void call(const std::string &path, const char *iface, const char *method,
            GVariant *args, Callback done) {
    if (!bus || calls >= 128) {
      done(nullptr, nullptr);
      return;
    }
    ++calls;
    auto *p = new Pending{weak_from_this(), generation, std::move(done)};
    g_dbus_connection_call(
        bus, "org.bluez", path.c_str(), iface, method, args, nullptr,
        G_DBUS_CALL_FLAGS_NONE, 10000, cancel,
        [](GObject *obj, GAsyncResult *result, gpointer user) {
          std::unique_ptr<Pending> p(static_cast<Pending *>(user));
          GError *error = nullptr;
          Variant value(g_dbus_connection_call_finish(G_DBUS_CONNECTION(obj),
                                                      result, &error));
          if (auto s = p->self.lock()) {
            --s->calls;
            if (s->generation == p->generation && s->mode != "off")
              p->fn(value.p, error);
          }
          if (error)
            g_error_free(error);
        },
        p);
  }
  void connectBus(bool advertise) {
    GError *error = nullptr;
    auto *address =
        g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (address)
      bus = g_dbus_connection_new_for_address_sync(
          address,
          static_cast<GDBusConnectionFlags>(
              G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
              G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
          nullptr, nullptr, &error);
    g_free(address);
    if (!bus) {
      if (error)
        g_error_free(error);
      failure("system-bus-unavailable");
      return;
    }
    g_dbus_connection_set_exit_on_close(bus, false);
    cancel = g_cancellable_new();
    Variant owner(g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner",
        g_variant_new("(s)", "org.bluez"), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, cancel, &error));
    if (owner.p) {
      const char *value;
      g_variant_get(owner.p, "(&s)", &value);
      bluezOwner = value;
    }
    if (error) {
      g_error_free(error);
      failure("bluez-unavailable");
      return;
    }
    properties = g_dbus_connection_signal_subscribe(
        bus, "org.bluez", "org.freedesktop.DBus.Properties",
        "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *path, const gchar *,
           const gchar *, GVariant *params, gpointer user) {
          auto *s = static_cast<Bluez *>(user);
          const char *iface;
          GVariant *changed;
          GVariant *invalidated;
          g_variant_get(params, "(&s@a{sv}@as)", &iface, &changed,
                        &invalidated);
          Variant ch(changed), inv(invalidated);
          auto found = s->objects.find(path);
          if (found != s->objects.end()) {
            GVariantDict dict;
            auto &old = found->second[iface];
            g_variant_dict_init(&dict, old.p);
            GVariantIter iter;
            g_variant_iter_init(&iter, changed);
            const char *key;
            GVariant *value;
            while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
              g_variant_dict_insert_value(&dict, key, value);
              g_variant_unref(value);
            }
            g_variant_iter_init(&iter, invalidated);
            while (g_variant_iter_next(&iter, "&s", &key))
              g_variant_dict_remove(&dict, key);
            old = Variant(g_variant_ref_sink(g_variant_dict_end(&dict)));
          }
          if (std::string(iface) == "org.bluez.GattCharacteristic1") {
            Variant value(
                g_variant_lookup_value(changed, "Value", G_VARIANT_TYPE("ay")));
            if (value.p) {
              std::shared_ptr<Link> target;
              for (auto &[id, l] : s->links)
                if (l->initiator && l->tx == path) {
                  target = l;
                  break;
                }
              if (target)
                s->receive(target, bytes(value.p));
            }
          }
          if (std::string(iface) == "org.bluez.Device1") {
            Variant connected(g_variant_lookup_value(changed, "Connected",
                                                     G_VARIANT_TYPE_BOOLEAN));
            if (connected.p && !g_variant_get_boolean(connected.p)) {
              auto l = s->forPeer(path);
              if (l)
                s->drop(l, true);
              s->sessions.disconnected(path);
            }
          }
          s->observe(path);
        },
        this, nullptr);
    added = g_dbus_connection_signal_subscribe(
        bus, "org.bluez", "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
           const gchar *, GVariant *params, gpointer user) {
          auto *s = static_cast<Bluez *>(user);
          const char *path;
          GVariant *interfaces;
          g_variant_get(params, "(&o@a{sa{sv}})", &path, &interfaces);
          Variant hold(interfaces);
          s->record(path, interfaces);
          s->observe(path);
        },
        this, nullptr);
    removed = g_dbus_connection_signal_subscribe(
        bus, "org.bluez", "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
           const gchar *, GVariant *params, gpointer user) {
          auto *s = static_cast<Bluez *>(user);
          const char *path;
          GVariant *interfaces;
          g_variant_get(params, "(&o@as)", &path, &interfaces);
          Variant hold(interfaces);
          s->objects.erase(path);
          if (std::string(path).starts_with(s->adapter + "/dev_") &&
              std::string(path).find('/', s->adapter.size() + 1) == std::string::npos) {
            if (auto link = s->forPeer(path)) s->drop(link, true);
            s->sessions.disconnected(path);
          }
          if (s->candidates.erase(path)) {
            Event e;
            e.type = DKBLE_EVENT_CANDIDATE_GONE;
            e.generation = s->generation;
            e.peer = path;
            s->emit(std::move(e));
          }
        },
        this, nullptr);
    snapshot([this, advertise](bool ok) {
      if (!ok) {
        failure("bluez-snapshot-failed");
        return;
      }
      for (auto &[path, interfaces] : objects)
        if (auto found = interfaces.find("org.bluez.Adapter1");
            found != interfaces.end() && boolean(found->second.p, "Powered")) {
          if (advertise &&
              (!interfaces.count("org.bluez.LEAdvertisingManager1") ||
               !interfaces.count("org.bluez.GattManager1")))
            continue;
          adapter = path;
          break;
        }
      if (adapter.empty()) {
        failure("bluetooth-off-or-role-unsupported", DKBLE_UNSUPPORTED);
        return;
      }
      if (advertise)
        publish();
      else
        scan();
    });
  }
  void record(const std::string &path, GVariant *values) {
    if (objects.size() >= 2048 && !objects.count(path)) {
      failure("bluez-object-limit", DKBLE_OVERFLOW);
      return;
    }
    GVariantIter iter;
    g_variant_iter_init(&iter, values);
    const char *iface;
    GVariant *properties;
    while (g_variant_iter_next(&iter, "{&s@a{sv}}", &iface, &properties))
      objects[path][iface] = Variant(properties);
  }
  void snapshot(std::function<void(bool)> done) {
    call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
         nullptr, [this, done = std::move(done)](GVariant *result, GError *) {
           if (!result) {
             done(false);
             return;
           }
           Variant values(g_variant_get_child_value(result, 0));
           GVariantIter iter;
           g_variant_iter_init(&iter, values.p);
           const char *path;
           GVariant *properties;
           while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path,
                                      &properties)) {
             Variant hold(properties);
             record(path, properties);
           }
           done(true);
         });
  }
  void scan() {
    GVariantBuilder filter;
    g_variant_builder_init(&filter, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&filter, "{sv}", "Transport",
                          g_variant_new_string("le"));
    call(adapter, "org.bluez.Adapter1", "SetDiscoveryFilter",
         g_variant_new("(a{sv})", &filter), [this](GVariant *value, GError *) {
           if (!value) {
             failure("scan-filter-failed");
             return;
           }
           call(adapter, "org.bluez.Adapter1", "StartDiscovery", nullptr,
                [this](GVariant *value, GError *) {
                  if (!value) {
                    failure("scan-start-failed");
                    return;
                  }
                  event(DKBLE_EVENT_STATE, "scanning");
                  snapshot([this](bool ok) {
                    if (ok)
                      for (auto &[path, interfaces] : objects)
                        observe(path);
                  });
                });
         });
  }
  void observe(const std::string &path) {
    auto found = objects.find(path);
    if (found == objects.end())
      return;
    if (path == adapter) {
      auto a = found->second.find("org.bluez.Adapter1");
      if (a != found->second.end() && !boolean(a->second.p, "Powered")) {
        event(DKBLE_EVENT_STATE, "bluetooth_off");
        std::vector<std::shared_ptr<Link>> all;
        for (auto &[id, l] : links)
          all.push_back(l);
        for (auto &l : all)
          drop(l, false);
        return;
      }
    }
    auto device = found->second.find("org.bluez.Device1");
    if (device == found->second.end())
      return;
    auto props = device->second.p;
    if (mode == "scan" && path.starts_with(adapter + "/dev_") &&
        contains(props, "UUIDs", config.service)) {
      if (candidates.size() < config.options.maximum_candidates ||
          candidates.count(path)) {
        candidates[path] = Clock::now();
        gint16 rssi = 0;
        g_variant_lookup(props, "RSSI", "n", &rssi);
        Event e;
        e.type = DKBLE_EVENT_CANDIDATE;
        e.generation = generation;
        e.peer = path;
        e.rssi = rssi;
        emit(std::move(e));
        if (wanted.count(path))
          connect(path, {});
      }
    }
    std::vector<std::shared_ptr<Link>> all;
    for (auto &[id, l] : links)
      if (l->peer == path)
        all.push_back(l);
    for (auto &l : all) {
      if (l->initiator && !l->ready && boolean(props, "ServicesResolved") &&
          l->rx.empty())
        resolve(l);
    }
  }
  std::shared_ptr<Link> forPeer(const std::string &peer) {
    for (auto &[id, l] : links)
      if (l->peer == peer)
        return l;
    return {};
  }
  bool alive(const std::shared_ptr<Link> &l) {
    auto found = links.find(l->id);
    return found != links.end() && found->second == l;
  }
  void connect(const std::string &peer, const std::string &probe) {
    if (mode != "scan" || !candidates.count(peer) || forPeer(peer) ||
        retries.count(peer) || links.size() >= config.options.maximum_links)
      return;
    auto l = std::make_shared<Link>();
    l->id = "bluez-" + std::to_string(++serial);
    l->peer = peer;
    l->probe = probe;
    l->initiator = true;
    l->deadline = Clock::now() +
                  std::chrono::milliseconds(config.options.connect_timeout_ms);
    auto dev = objects[peer].find("org.bluez.Device1");
    l->owned =
        dev == objects[peer].end() || !boolean(dev->second.p, "Connected");
    links[l->id] = l;
    call(peer, "org.bluez.Device1", "Connect", nullptr,
         [this, l](GVariant *value, GError *error) {
           if (!alive(l))
             return;
           if (!value && (!error || !g_error_matches(error, G_IO_ERROR,
                                                     G_IO_ERROR_CANCELLED))) {
             event(DKBLE_EVENT_DIAGNOSTIC,
                   std::string("gatt-connect-failed:") +
                       (error ? error->message : "unknown"));
             drop(l, true);
             return;
           }
           resolve(l);
         });
  }
  void resolve(std::shared_ptr<Link> l) {
    if (!alive(l) || !l->rx.empty())
      return;
    l->resolveAt = Clock::now() + 500ms;
    l->rx = "resolving";
    snapshot([this, l](bool ok) {
      if (!alive(l))
        return;
      l->rx.clear();
      if (!ok) {
        drop(l, true);
        return;
      }
      std::set<std::string> services;
      for (auto &[path, ifs] : objects)
        if (path.starts_with(l->peer + "/"))
          if (auto s = ifs.find("org.bluez.GattService1");
              s != ifs.end() && string(s->second.p, "UUID") == config.service)
            services.insert(path);
      for (auto &[path, ifs] : objects)
        if (auto c = ifs.find("org.bluez.GattCharacteristic1");
            c != ifs.end() && services.count(object(c->second.p, "Service"))) {
          auto id = string(c->second.p, "UUID");
          if (id == config.receive && contains(c->second.p, "Flags", "write"))
            l->rx = path;
          if (id == config.notify &&
              (contains(c->second.p, "Flags", "notify") ||
               contains(c->second.p, "Flags", "read"))) {
            l->tx = path;
            l->polling = !contains(c->second.p, "Flags", "notify");
          }
          if (id == config.receive)
            l->mtu =
                std::clamp<size_t>(number(c->second.p, "MTU", 23) - 3, 20, 512);
        }
      if (l->rx.empty() || l->tx.empty()) {
        // A partial object snapshot must not make RX's path a permanent
        // "resolved" sentinel while TX is still arriving.
        l->rx.clear();
        l->tx.clear();
        return;
      }
      if (l->polling) {
        beginReadSession(l);
        return;
      }
      call(l->tx, "org.bluez.GattCharacteristic1", "StartNotify", nullptr,
           [this, l](GVariant *value, GError *) {
             if (!alive(l))
               return;
             if (!value) {
               drop(l, true);
               return;
             }
             ready(l);
           });
    });
  }
  void beginReadSession(std::shared_ptr<Link> l) {
    size_t offset = 0;
    while (offset < l->epoch.size()) {
      const auto n = getrandom(l->epoch.data() + offset, l->epoch.size() - offset, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { drop(l, true); return; }
      offset += static_cast<size_t>(n);
    }
    if (!read_session::valid(l->epoch)) { drop(l, true); return; }
    auto packet = read_session::packet(read_session::hello, l->epoch);
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "type", g_variant_new_string("request"));
    l->writing = true;
    call(l->rx, "org.bluez.GattCharacteristic1", "WriteValue",
         g_variant_new("(@aya{sv})", g_variant_new_fixed_array(
             G_VARIANT_TYPE_BYTE, packet.data(), packet.size(), 1), &options),
         [this, l](GVariant *value, GError *) {
           if (!alive(l)) return;
           l->writing = false;
           if (!value) { drop(l, true); return; }
           l->helloSent = true;
           read(l);
         });
  }
  void read(std::shared_ptr<Link> l) {
    if (!alive(l) || !l->polling || !l->helloSent || l->reading || l->writing ||
        Clock::now() < l->readAt)
      return;
    l->reading = true;
    call(l->tx, "org.bluez.GattCharacteristic1", "ReadValue",
         g_variant_new("(@a{sv})", dictionary()),
         [this, l](GVariant *value, GError *error) {
           if (!alive(l))
             return;
           l->reading = false;
           l->readAt =
               Clock::now() + std::chrono::milliseconds(config.readInterval);
           if (!value) {
             if (error && g_dbus_error_is_remote_error(error)) {
               auto *name = g_dbus_error_get_remote_error(error);
               bool busy = std::string(name) == "org.bluez.Error.InProgress";
               g_free(name);
               if (busy)
                 return;
             }
             event(DKBLE_EVENT_DIAGNOSTIC, "gatt-read-failed");
             drop(l, true);
             return;
           }
           Variant payload(g_variant_get_child_value(value, 0));
           auto data = bytes(payload.p);
           uint8_t kind;
           read_session::Epoch epoch;
           if (!read_session::parse(data, kind, epoch) || kind > 1) {
             drop(l, true);
             return;
           }
           if (epoch != l->epoch) {
             event(DKBLE_EVENT_DIAGNOSTIC, "stale-read-epoch");
             return;
           }
           data.erase(data.begin(), data.begin() + read_session::header);
           ready(l);
           if (!data.empty()) {
             l->readAt = Clock::now();
             receive(l, std::move(data));
           }
         });
  }
  void ready(std::shared_ptr<Link> l) {
    if (!alive(l) || l->ready)
      return;
    l->ready = true;
    linkEvent(DKBLE_EVENT_LINK, *l);
    auto early = std::move(l->early);
    l->earlyBytes = 0;
    for (auto &data : early)
      receive(l, std::move(data));
  }
  void receive(std::shared_ptr<Link> l, std::vector<uint8_t> data) {
    if (!alive(l))
      return;
    if (data.empty() || data.size() > 512) {
      drop(l, true);
      return;
    }
    if (!l->ready) {
      if (l->early.size() >= 128 || l->earlyBytes + data.size() > 4096) {
        drop(l, true);
        return;
      }
      l->earlyBytes += data.size();
      l->early.push_back(std::move(data));
    } else
      linkEvent(DKBLE_EVENT_DATA, *l, std::move(data));
  }
  void complete(const std::shared_ptr<Link> &l, int status) {
    if (!l->sending)
      return;
    l->sending = false;
    l->writing = false;
    l->pending.clear();
    Event e;
    e.type = DKBLE_EVENT_SEND_COMPLETE;
    e.generation = generation;
    e.link = l->id;
    e.request = l->request;
    e.status = status;
    emit(std::move(e));
  }
  void drop(std::shared_ptr<Link> l, bool retry) {
    if (!alive(l))
      return;
    links.erase(l->id);
    if (!l->initiator) sessions.retire(l->peer, l->epoch);
    complete(l, DKBLE_IO);
    if (bus && l->initiator) {
      if (!l->tx.empty() && !l->polling)
        call(l->tx, "org.bluez.GattCharacteristic1", "StopNotify", nullptr,
             [](GVariant *, GError *) {});
      if (l->owned)
        call(l->peer, "org.bluez.Device1", "Disconnect", nullptr,
             [](GVariant *, GError *) {});
    }
    if (l->ready)
      linkEvent(DKBLE_EVENT_DISCONNECTED, *l);
    if (!l->probe.empty()) {
      wanted.erase(l->peer);
      retries.erase(l->peer);
      linkEvent(DKBLE_EVENT_PROBE_ENDED, *l);
    } else if (retry && mode == "scan" && wanted.count(l->peer))
      retries[l->peer] = Clock::now() + std::chrono::milliseconds(
                                            config.options.retry_delay_ms);
  }
  void send(std::shared_ptr<Link> l) {
    if (!alive(l) || !l->sending || l->writing)
      return;
    if (l->offset == l->pending.size()) {
      complete(l, 0);
      return;
    }
    auto count = std::min({size_t(512), l->mtu, l->pending.size() - l->offset});
    if (l->polling) count = std::min(count, size_t{20} - read_session::header);
    // Server TX is consumed by a device-scoped ReadValue, never broadcast.
    if (!l->initiator || l->reading || Clock::now() < l->writeAt)
      return;
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "type",
                          g_variant_new_string("request"));
    auto slice = std::span(l->pending).subspan(l->offset, count);
    auto packet = l->polling ? read_session::packet(read_session::write, l->epoch, slice)
                            : std::vector<uint8_t>(slice.begin(), slice.end());
    l->writing = true;
    call(l->rx, "org.bluez.GattCharacteristic1", "WriteValue",
         g_variant_new("(@aya{sv})",
                       g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                 packet.data(), packet.size(), 1),
                       &options),
         [this, l, count](GVariant *value, GError *error) {
           if (!alive(l))
             return;
           l->writing = false;
           if (!value) {
             std::string name = "unknown";
             if (error && g_dbus_error_is_remote_error(error)) {
               auto *raw = g_dbus_error_get_remote_error(error);
               name = raw;
               g_free(raw);
             }
             if (name == "org.bluez.Error.InProgress") {
               l->writeAt = Clock::now() +
                            std::chrono::milliseconds(config.readInterval);
               return;
             }
             event(DKBLE_EVENT_DIAGNOSTIC,
                   "gatt-write-failed:" + name +
                       (error ? std::string(":") + error->message : ""));
             drop(l, true);
             return;
           }
           l->offset += count;
           send(l);
         });
  }
  GVariant *props(const std::string &path, const char *iface) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    if (std::string(iface) == "org.bluez.GattService1") {
      g_variant_builder_add(&b, "{sv}", "UUID",
                            g_variant_new_string(config.service.c_str()));
      g_variant_builder_add(&b, "{sv}", "Primary", g_variant_new_boolean(true));
      g_variant_builder_add(
          &b, "{sv}", "Includes",
          g_variant_new_array(G_VARIANT_TYPE_OBJECT_PATH, nullptr, 0));
    } else if (std::string(iface) == "org.bluez.GattCharacteristic1") {
      bool output = path == tx;
      g_variant_builder_add(
          &b, "{sv}", "UUID",
          g_variant_new_string(
              (output ? config.notify : config.receive).c_str()));
      g_variant_builder_add(&b, "{sv}", "Service",
                            g_variant_new_object_path(service.c_str()));
      const char *flags[] = {output ? "read" : "write", nullptr};
      g_variant_builder_add(&b, "{sv}", "Flags", g_variant_new_strv(flags, -1));
    } else {
      g_variant_builder_add(&b, "{sv}", "Type",
                            g_variant_new_string("peripheral"));
      const char *ids[] = {config.service.c_str(), nullptr};
      g_variant_builder_add(&b, "{sv}", "ServiceUUIDs",
                            g_variant_new_strv(ids, -1));
    }
    return g_variant_builder_end(&b);
  }
  static GVariant *property(GDBusConnection *, const gchar *, const gchar *path,
                            const gchar *iface, const gchar *name, GError **,
                            gpointer user) {
    auto *s = static_cast<Bluez *>(user);
    Variant values(g_variant_ref_sink(s->props(path, iface)));
    return g_variant_lookup_value(values.p, name, nullptr);
  }
  static void method(GDBusConnection *, const gchar *sender, const gchar *path,
                     const gchar *, const gchar *name, GVariant *parameters,
                     GDBusMethodInvocation *invocation, gpointer user) {
    auto *s = static_cast<Bluez *>(user);
    if (s->bluezOwner != sender) {
      g_dbus_method_invocation_return_dbus_error(
          invocation, "org.bluez.Error.NotAuthorized",
          "Only bluetoothd may use the exported GATT service");
      return;
    }
    if (std::string(name) == "GetManagedObjects") {
      GVariantBuilder objects;
      g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
      for (auto &p : {s->service, s->rx, s->tx}) {
        GVariantBuilder interfaces;
        g_variant_builder_init(&interfaces, G_VARIANT_TYPE("a{sa{sv}}"));
        const char *iface = p == s->service ? "org.bluez.GattService1"
                                            : "org.bluez.GattCharacteristic1";
        g_variant_builder_add(&interfaces, "{s@a{sv}}", iface,
                              s->props(p, iface));
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", p.c_str(), &interfaces);
      }
      g_dbus_method_invocation_return_value(
          invocation, g_variant_new("(a{oa{sa{sv}}})", &objects));
      return;
    }
    if (std::string(name) == "Release") {
      g_dbus_method_invocation_return_value(invocation, nullptr);
      s->event(DKBLE_EVENT_STATE, "unavailable");
      return;
    }
    Variant options(g_variant_get_child_value(
        parameters, std::string(name) == "WriteValue" ? 1 : 0));
    auto device = object(options.p, "device");
    auto l = s->forPeer(device);
    if (std::string(name) == "ReadValue" && std::string(path) == s->tx &&
        device.starts_with(s->adapter + "/dev_") &&
        number(options.p, "offset", 0) == 0) {
      if (!l || l->initiator || !read_session::valid(l->epoch)) {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.bluez.Error.NotPermitted", "Session handshake required");
        return;
      }
      l->mtu = std::clamp<size_t>(number(options.p, "mtu", 23) - 3, 20, 512);
#ifdef LIBBLE_RADIO_TRACE
      s->event(DKBLE_EVENT_DIAGNOSTIC,
               "read " + l->id + " n=" + std::to_string(++l->readCount) +
                   " mtu=" + std::to_string(l->mtu) +
                   " offset=" + std::to_string(l->offset));
#endif
      s->ready(l);
      size_t count =
          l->sending ? std::min(l->mtu - read_session::header, l->pending.size() - l->offset) : 0;
      auto packet = read_session::packet(count ? read_session::payload : read_session::idle, l->epoch);
      if (count)
        packet.insert(packet.end(), l->pending.begin() + l->offset,
                      l->pending.begin() + l->offset + count);
      auto *response = g_variant_new(
          "(@ay)", g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, packet.data(),
                                             packet.size(), 1));
      g_dbus_method_invocation_return_value(invocation, response);
      if (l->sending) {
        l->offset += count;
        if (l->offset == l->pending.size())
          s->complete(l, 0);
      }
      return;
    }
    if (std::string(name) == "WriteValue" && std::string(path) == s->rx &&
        device.starts_with(s->adapter + "/dev_") &&
        number(options.p, "offset", 0) == 0 &&
        !boolean(options.p, "prepare-authorize") &&
        (string(options.p, "type").empty() ||
         string(options.p, "type") == "request")) {
      Variant value(g_variant_get_child_value(parameters, 0));
      auto data = bytes(value.p);
      uint8_t kind;
      read_session::Epoch epoch;
      if (read_session::parse(data, kind, epoch) && kind == read_session::hello &&
          (!l || !l->initiator) && (l || s->links.size() < s->config.options.maximum_links)) {
        const auto result = s->sessions.accept(device, epoch, s->config.options.maximum_candidates);
        if (result != read_session::Registry::rejected &&
            (result != read_session::Registry::current || l)) {
          if (result == read_session::Registry::fresh) {
            if (l) s->drop(l, false);
            l = std::make_shared<Link>();
            l->id = "bluez-" + std::to_string(++s->serial);
            l->peer = device;
            l->epoch = epoch;
            s->links[l->id] = l;
            s->event(DKBLE_EVENT_DIAGNOSTIC, "read-session-open " + l->id);
          }
          g_dbus_method_invocation_return_value(invocation, nullptr);
          s->ready(l);
          return;
        }
      }
      if (read_session::parse(data, kind, epoch) && kind == read_session::write &&
          l && !l->initiator && l->ready && epoch == l->epoch) {
        data.erase(data.begin(), data.begin() + read_session::header);
#ifdef LIBBLE_RADIO_TRACE
        s->event(DKBLE_EVENT_DIAGNOSTIC,
                 "write " + l->id + " n=" + std::to_string(++l->writeCount) +
                     " bytes=" + std::to_string(data.size()) +
                     " dbus=" + std::to_string(g_dbus_message_get_serial(
                         g_dbus_method_invocation_get_message(invocation))) +
                     " us=" + std::to_string(g_get_real_time()));
#endif
        // Queue the ATT application's response before publishing bytes. The
        // consumer may finish its round and request shutdown from a callback.
        g_dbus_method_invocation_return_value(invocation, nullptr);
#ifdef LIBBLE_RADIO_TRACE
        s->event(DKBLE_EVENT_DIAGNOSTIC,
                 "write-return " + l->id + " n=" +
                     std::to_string(l->writeCount) + " us=" +
                     std::to_string(g_get_real_time()));
        struct Flushed {
          std::weak_ptr<Bluez> self;
          std::string link;
          uint64_t generation;
          size_t count;
        };
        auto *trace = new Flushed{s->weak_from_this(), l->id,
                                 s->generation, l->writeCount};
        g_dbus_connection_flush(s->bus, nullptr,
          [](GObject *bus, GAsyncResult *result, gpointer user) {
            std::unique_ptr<Flushed> trace(static_cast<Flushed *>(user));
            GError *error = nullptr;
            const bool ok = g_dbus_connection_flush_finish(
                G_DBUS_CONNECTION(bus), result, &error);
            if (auto s = trace->self.lock(); s &&
                s->generation == trace->generation && s->mode != "off")
              s->event(DKBLE_EVENT_DIAGNOSTIC,
                       "write-flush " + trace->link + " n=" +
                           std::to_string(trace->count) + " ok=" +
                           (ok ? "1" : "0") + " us=" +
                           std::to_string(g_get_real_time()));
            if (error) g_error_free(error);
          }, trace);
#endif
        s->receive(l, std::move(data));
        return;
      }
    }
    s->event(DKBLE_EVENT_DIAGNOSTIC, std::string("gatt-reject-") + name +
                                         " type=" + string(options.p, "type") +
                                         " subscribed=" + (l ? "yes" : "no"));
    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.bluez.Error.NotSupported",
        "Unsupported operation or unsubscribed peer");
  }
  void publish() {
    info = g_dbus_node_info_new_for_xml(xml, nullptr);
    const GDBusInterfaceVTable vtable{method, property, nullptr, {nullptr}};
    for (auto &[path, iface] :
         std::vector<std::pair<std::string, const char *>>{
             {root, "org.freedesktop.DBus.ObjectManager"},
             {service, "org.bluez.GattService1"},
             {rx, "org.bluez.GattCharacteristic1"},
             {tx, "org.bluez.GattCharacteristic1"},
             {advert, "org.bluez.LEAdvertisement1"}}) {
      auto id = g_dbus_connection_register_object(
          bus, path.c_str(), g_dbus_node_info_lookup_interface(info, iface),
          &vtable, this, nullptr, nullptr);
      if (!id) {
        failure("gatt-export-failed");
        return;
      }
      registered.push_back(id);
    }
    call(adapter, "org.bluez.GattManager1", "RegisterApplication",
         g_variant_new("(o@a{sv})", root.c_str(), dictionary()),
         [this](GVariant *value, GError *) {
           if (!value) {
             failure("gatt-register-failed");
             return;
           }
           call(adapter, "org.bluez.LEAdvertisingManager1",
                "RegisterAdvertisement",
                g_variant_new("(o@a{sv})", advert.c_str(), dictionary()),
                [this](GVariant *value, GError *) {
                  if (!value) {
                    failure("advertise-register-failed");
                    return;
                  }
                  event(DKBLE_EVENT_STATE, "advertising");
                });
         });
  }
  int stop() {
    int status = DKBLE_OK;
    mode = "off";
    if (cancel) {
      g_cancellable_cancel(cancel);
      g_object_unref(cancel);
      cancel = nullptr;
    }
    for (auto &[id, l] : links) {
      if (bus && l->initiator && l->owned) {
        GError *error = nullptr;
        Variant reply(g_dbus_connection_call_sync(
            bus, "org.bluez", l->peer.c_str(), "org.bluez.Device1",
            "Disconnect", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000,
            nullptr, &error));
        if (error)
          g_error_free(error);
      }
    }
    links.clear();
    sessions.clear();
    wanted.clear();
    retries.clear();
    candidates.clear();
    objects.clear();
    adapter.clear();
    if (bus) {
      for (auto id : registered)
        g_dbus_connection_unregister_object(bus, id);
      registered.clear();
      for (auto id : {properties, added, removed})
        if (id)
          g_dbus_connection_signal_unsubscribe(bus, id);
      GError *error = nullptr;
      if (!g_dbus_connection_is_closed(bus) &&
          !g_dbus_connection_close_sync(bus, nullptr, &error))
        status = DKBLE_IO;
      if (error)
        g_error_free(error);
      g_object_unref(bus);
      bus = nullptr;
    }
    properties = added = removed = 0;
    if (info) {
      g_dbus_node_info_unref(info);
      info = nullptr;
    }
    return status;
  }
  void handle(Command c) {
    if (c.op == Op::stop) {
      stop();
      generation = c.generation;
      return;
    }
    if (c.op == Op::startScan || c.op == Op::startAdvertise) {
      stop();
      generation = c.generation;
      mode = c.op == Op::startScan ? "scan" : "advertise";
      event(DKBLE_EVENT_STATE, "starting");
      connectBus(mode == "advertise");
      return;
    }
    if (c.generation != generation || mode == "off")
      return;
    auto invalid = [&] {
      Event e;
      e.type = c.op == Op::send ? DKBLE_EVENT_SEND_COMPLETE : DKBLE_EVENT_ERROR;
      e.generation = generation;
      e.request = c.request;
      e.link = c.value;
      e.status = DKBLE_STATE;
      e.detail = "link-unavailable";
      emit(std::move(e));
    };
    if (c.op == Op::connect || c.op == Op::probe) {
      bool probing = false;
      for (auto &[id, l] : links)
        probing |= !l->probe.empty();
      if (mode != "scan" || !candidates.count(c.value) ||
          (c.op == Op::probe &&
           (wanted.count(c.value) || forPeer(c.value) || probing)) ||
          links.size() >= config.options.maximum_links ||
          (wanted.size() >= config.options.maximum_links &&
           !wanted.count(c.value))) {
        invalid();
        return;
      }
      if (c.op == Op::connect)
        wanted.insert(c.value);
      connect(c.value, c.op == Op::probe ? c.extra : "");
      return;
    }
    if (c.op == Op::cancel || c.op == Op::adopt) {
      std::shared_ptr<Link> l;
      for (auto &[id, link] : links)
        if (link->probe == c.value)
          l = link;
      if (c.op == Op::cancel) {
        if (l)
          drop(l, false);
        return;
      }
      auto old = forPeer(c.extra);
      if (!l || !l->ready || (old && old != l && old->ready) ||
          (wanted.size() >= config.options.maximum_links &&
           !wanted.count(c.extra) && !wanted.count(l->peer))) {
        invalid();
        return;
      }
      wanted.erase(c.extra);
      retries.erase(c.extra);
      if (old && old != l)
        drop(old, false);
      wanted.insert(l->peer);
      l->probe.clear();
      return;
    }
    auto found = links.find(c.value);
    auto l = found == links.end() ? nullptr : found->second;
    if (c.op == Op::disconnect || c.op == Op::reset) {
      if (l) {
        if (c.op == Op::disconnect)
          wanted.erase(l->peer);
        drop(l, c.op == Op::reset);
      }
      return;
    }
    if (c.op == Op::send) {
      if (!l || !l->ready || l->sending) {
        invalid();
        return;
      }
      l->pending = std::move(c.data);
      l->offset = 0;
      l->request = c.request;
      l->sending = true;
      l->deadline = Clock::now() +
                    std::chrono::milliseconds(config.options.send_timeout_ms);
      send(l);
      return;
    }
    if (c.op == Op::recover) {
      snapshot([this](bool ok) {
        if (ok)
          for (auto &[path, ifs] : objects)
            observe(path);
      });
      return;
    }
  }
  void tick() {
    std::deque<Command> batch;
    bool ending;
    uint64_t revision;
    std::optional<CommandQueue::Stop> control;
    {
      std::lock_guard lock(mutex);
      control = commands.take_stop();
      revision = commands.take(batch);
      ending = closing;
      if (configDirty) {
        config = pendingConfig;
        configDirty = false;
      }
    }
    if (ending) {
      g_main_loop_quit(loop);
      return;
    }
    if (control) {
      const auto result = stop();
      generation = control->generation;
      std::lock_guard lock(mutex);
      completedStop = control->ticket;
      stopStatus = result;
      stoppedCondition.notify_all();
    }
    for (auto &c : batch) {
      {
        std::lock_guard lock(mutex);
        if (!commands.allows(c.generation, revision))
          continue;
      }
      handle(std::move(c));
    }
    auto now = Clock::now();
    std::vector<std::shared_ptr<Link>> all;
    for (auto &[id, l] : links)
      all.push_back(l);
    for (auto &l : all) {
      if ((!l->ready || l->sending) && now >= l->deadline) {
        event(DKBLE_EVENT_DIAGNOSTIC, l->ready ? "send-timeout" : "connect-timeout");
        drop(l, true);
        continue;
      }
      if (l->initiator && !l->ready && l->rx.empty() && now >= l->resolveAt) {
        auto peer = objects.find(l->peer);
        if (peer != objects.end()) {
          auto device = peer->second.find("org.bluez.Device1");
          if (device != peer->second.end() &&
              boolean(device->second.p, "Connected") &&
              boolean(device->second.p, "ServicesResolved"))
            resolve(l);
        }
      }
      if (l->polling)
        read(l);
      if (l->sending)
        send(l);
    }
    std::vector<std::string> due;
    for (auto it = retries.begin(); it != retries.end();)
      if (now >= it->second) {
        due.push_back(it->first);
        it = retries.erase(it);
      } else
        ++it;
    for (auto &peer : due)
      connect(peer, {});
    for (auto it = candidates.begin(); it != candidates.end();)
      if (now - it->second >
              std::chrono::milliseconds(config.options.candidate_ttl_ms) &&
          !wanted.count(it->first) && !forPeer(it->first)) {
        Event e;
        e.type = DKBLE_EVENT_CANDIDATE_GONE;
        e.generation = generation;
        e.peer = it->first;
        emit(std::move(e));
        it = candidates.erase(it);
      } else
        ++it;
  }
};
class LinuxBackend final : public Backend {
  std::shared_ptr<Bluez> state;

public:
  LinuxBackend(Config c, Emit e)
      : state(std::make_shared<Bluez>(std::move(c), std::move(e))) {
    state->start();
  }
  ~LinuxBackend() override { close(); }
  int set_read_interval(uint32_t interval) override {
    std::lock_guard lock(state->mutex);
    state->pendingConfig.readInterval = interval;
    state->configDirty = true;
    return 0;
  }
  int set_options(const dkble_options &options) override {
    std::lock_guard lock(state->mutex);
    state->pendingConfig.options = options;
    state->configDirty = true;
    return 0;
  }
  int command(Command c) override {
    return state ? state->command(std::move(c)) : DKBLE_STATE;
  }
  void close() override {
    if (state) {
      state->close();
      state.reset();
    }
  }
};
} // namespace
std::unique_ptr<Backend> make_backend(Config c, Emit e, Wake) {
  return std::make_unique<LinuxBackend>(std::move(c), std::move(e));
}
} // namespace dkble
