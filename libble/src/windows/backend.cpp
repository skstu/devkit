#include "../common/backend.hpp"
#include "../common/read_session.hpp"
#include <algorithm>
#include <memory>
#include <objbase.h>
#include <variant>
#include <windows.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include "nearby_ble_policy.h"

namespace {
namespace read_session = dkble::read_session;
using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Radios;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using Value = std::variant<std::monostate, std::string, bool, int32_t,
                           std::vector<uint8_t>>;
using Map = std::map<Value, Value>;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Reply {
  std::function<void(const std::string &)> completion;
  void Ok() {
    if (auto fn = std::move(completion))
      fn("");
  }
  void Error(const std::string &error) {
    if (auto fn = std::move(completion))
      fn(error);
  }
};
using Response = std::shared_ptr<Reply>;
using Failure = std::function<void(const std::string &)>;

void Trace(const char *stage, int32_t code) {
#ifdef SOVKIT_BLE_TEST_TRACE
  std::fprintf(stderr, "BLE %s: 0x%08x\n", stage,
               static_cast<unsigned int>(code));
#else
  (void)stage;
  (void)code;
#endif
}

std::string ErrorCode() {
  try {
    throw;
  } catch (const hresult_error &error) {
    Trace("WinRT failure", error.code().value);
    return error.code() == E_ACCESSDENIED ? "permission_denied" : "unavailable";
  } catch (...) {
    return "unavailable";
  }
}
std::string BluetoothFailure(BluetoothError error) {
  if (error == BluetoothError::RadioNotAvailable)
    return "bluetooth_off";
  if (error == BluetoothError::NotSupported)
    return "unsupported";
  if (error == BluetoothError::DisabledByPolicy)
    return "permission_denied";
  return "unavailable";
}
template <class Action> void IgnoreFailure(Action action) noexcept {
  try {
    action();
  } catch (...) {
  }
}
std::vector<uint8_t> Bytes(const IBuffer &buffer) {
  if (!buffer || buffer.Length() > 512)
    return {};
  std::vector<uint8_t> bytes(buffer.Length());
  DataReader::FromBuffer(buffer).ReadBytes(bytes);
  return bytes;
}
std::string Argument(const Map &args, const char *key) {
  const auto found = args.find(Value(key));
  if (found == args.end())
    return {};
  const auto value = std::get_if<std::string>(&found->second);
  return value ? *value : std::string{};
}
std::string UniqueId() {
  GUID id{};
  check_hresult(CoCreateGuid(&id));
  return to_string(to_hstring(id));
}
std::string AdvertisementHint(const std::vector<uint8_t> &bytes,
                              const guid &service) {
  if (bytes.size() != 24 ||
      !std::equal(bytes.begin(), bytes.begin() + 16,
                  reinterpret_cast<const uint8_t *>(&service)))
    return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string value;
  for (size_t i = 16; i < bytes.size(); ++i) {
    value += hex[bytes[i] >> 4];
    value += hex[bytes[i] & 15];
  }
  return value;
}
struct WriteDeferral {
  explicit WriteDeferral(Deferral input) : value(std::move(input)) {}
  WriteDeferral(const WriteDeferral &) = delete;
  Deferral value;
  ~WriteDeferral() {
    IgnoreFailure([&] { value.Complete(); });
  }
};
} // namespace

struct WindowsState : std::enable_shared_from_this<WindowsState> {
  using State = WindowsState;
  explicit WindowsState(dkble::Config c, dkble::Emit e, dkble::Wake w)
      : config(std::move(c)), emit(std::move(e)), wake(std::move(w)),
        kService(config.service), kRx(config.receive), kTx(config.notify) {}
  struct Link {
    std::string id = UniqueId(), peer, device_key, probe;
    bool initiator = false, ready = false, polling = false, reading = false,
         writing = false, helloSent = false;
    read_session::Epoch epoch{};
    Clock::time_point read_at{};
    BluetoothLEDevice device{nullptr};
    GattDeviceService service{nullptr};
    GattSession session{nullptr};
    GattCharacteristic write{nullptr}, notify{nullptr};
    GattSubscribedClient subscriber{nullptr};
    event_token connection_token{}, services_token{}, notify_token{},
        session_token{};
    Clock::time_point deadline = Clock::now() + 20s;
    std::vector<uint8_t> pending;
    size_t offset = 0, early_bytes = 0;
    std::vector<std::vector<uint8_t>> early;
    Response sending;
  };
  struct Peer {
    uint64_t address;
    BluetoothAddressType type;
    Clock::time_point seen;
  };
  struct Operation {
    IAsyncInfo async;
    Failure failure;
    std::string owner;
    Clock::time_point deadline;
  };

  dkble::Config config;
  dkble::Emit emit;
  dkble::Wake wake;
  guid kService, kRx, kTx;
  uint64_t abi_generation = 0;
  // Only this queue is touched outside the owner thread. Queued callbacks
  // hold weak references; late Windows completions cannot revive a bridge.
  std::mutex queue_mutex;
  std::deque<std::function<void()>> queue;
  bool closed = false, overflow = false, wake_pending = false;
  uint64_t generation = 0, next_operation = 0;
  std::map<uint64_t, Operation> operations;
  std::string mode = "off";
  bool publishing = false, waiting_for_radio = false;
  Response starting;
  Clock::time_point start_deadline;
  Clock::time_point advertising_deadline{};
  Clock::time_point radio_recovery_deadline{}, radio_retry_at{};
  BluetoothAdapter adapter{nullptr};
  Radio radio{nullptr};
  event_token radio_token{};
  BluetoothLEAdvertisementWatcher watcher{nullptr};
  event_token received_token{}, stopped_token{};
  GattServiceProvider provider{nullptr};
  GattLocalCharacteristic local_rx{nullptr}, local_tx{nullptr};
  event_token advertising_token{}, write_token{}, subscribers_token{};
  std::map<std::string, Peer> peers;
  std::map<std::string, std::string> hints;
  std::map<std::string, std::shared_ptr<Link>> links;
  std::set<std::string> wanted;
  std::set<std::string> rejected_subscribers;
  std::map<std::string, Clock::time_point> retries;

  void Handle(const std::string &method, const Map &args, Response reply);
  void Emit(Map value) {
    dkble::Event e;
    e.generation = abi_generation;
    const std::map<std::string, uint32_t> types = {
        {"state", 1},     {"candidate", 2},    {"candidateGone", 3},
        {"link", 4},      {"disconnected", 5}, {"data", 6},
        {"probeEnded", 7}};
    auto type = Argument(value, "type");
    auto found = types.find(type);
    if (found == types.end())
      return;
    e.type = found->second;
    e.link = Argument(value, "link");
    e.peer = Argument(value, "peer");
    e.probe = Argument(value, "probe");
    if (e.probe.empty())
      e.probe = Argument(value, "token");
    e.detail = Argument(value, "state");
    if (auto it = value.find(Value("rssi")); it != value.end())
      if (auto p = std::get_if<int32_t>(&it->second))
        e.rssi = *p;
    if (auto it = value.find(Value("initiator")); it != value.end())
      if (auto p = std::get_if<bool>(&it->second))
        e.initiator = *p;
    if (auto it = value.find(Value("data")); it != value.end())
      if (auto p = std::get_if<std::vector<uint8_t>>(&it->second))
        e.data = std::move(*p);
    emit(std::move(e));
  }
  void Status(const std::string &status) {
    Emit({{Value("type"), Value("state")}, {Value("state"), Value(status)}});
  }
  bool Alive(const std::shared_ptr<Link> &link) const {
    const auto found = links.find(link->id);
    return found != links.end() && found->second == link;
  }
  std::shared_ptr<Link> ForPeer(const std::string &peer) const {
    for (const auto &entry : links)
      if (entry.second->peer == peer)
        return entry.second;
    return nullptr;
  }
  void Post(std::function<void()> task) {
    std::lock_guard<std::mutex> guard(queue_mutex);
    if (closed)
      return;
    if (queue.size() >= 256)
      overflow = true;
    else
      queue.push_back(std::move(task));
    if (!wake_pending) {
      wake_pending = true;
      wake();
    }
  }
  template <class Action>
  static void Dispatch(std::weak_ptr<State> weak, uint64_t epoch,
                       Action action) {
    if (auto self = weak.lock())
      self->Post([weak, epoch, action = std::move(action)] {
        if (auto current = weak.lock(); current && current->generation == epoch)
          action(*current);
      });
  }
  template <class Async, class Success>
  void Await(Async async, Success success, Failure failure,
             std::string owner = {}) {
    if (operations.size() >= 128)
      throw hresult_error(E_OUTOFMEMORY);
    const auto id = ++next_operation;
    operations.emplace(
        id,
        Operation{async.template as<IAsyncInfo>(), failure, std::move(owner),
                  Clock::now() + std::chrono::milliseconds(
                                     config.options.send_timeout_ms)});
    auto weak = weak_from_this();
    async.Completed([weak, id, success](const auto &completed, AsyncStatus) {
      if (auto self = weak.lock())
        self->Post([weak, id, completed, success] {
          auto current = weak.lock();
          if (!current)
            return;
          auto found = current->operations.find(id);
          if (found == current->operations.end())
            return;
          auto failed = std::move(found->second.failure);
          current->operations.erase(found);
          try {
            success(completed.GetResults());
          } catch (...) {
            failed(ErrorCode());
          }
        });
    });
  }
  void Cancel(const std::string *owner = nullptr) {
    std::vector<Operation> cancelled;
    for (auto it = operations.begin(); it != operations.end();) {
      if (!owner || it->second.owner == *owner) {
        cancelled.push_back(std::move(it->second));
        it = operations.erase(it);
      } else
        ++it;
    }
    for (auto &operation : cancelled) {
      IgnoreFailure([&] { operation.async.Cancel(); });
      operation.failure("link_closed");
    }
  }
  void Drain();
  void Tick();
  void Stop();
  void Close();
  void Drop(std::shared_ptr<Link> link, bool retry = false);
  void DisconnectLink(const std::shared_ptr<Link> &link, bool retry);
  void ClearTransport();
  void Start(const std::string &selected, Response reply);
  void StartFailed(const std::string &code);
  void BeginRole();
  void RadioChanged();
  void WaitForRadio();
  void Scan();
  void Discovered(uint64_t address, BluetoothAddressType type, int rssi,
                  const std::string &hint);
  void Connect(const std::string &peer, const std::string &probe = {});
  void BeginReadSession(const std::shared_ptr<Link> &link);
  void Read(const std::shared_ptr<Link> &link);
  void Ready(const std::shared_ptr<Link> &link);
  void Receive(const std::shared_ptr<Link> &link, std::vector<uint8_t> bytes);
  void Publish();
  void AdvertisementChanged(BluetoothError error);
  void Subscribers();
  void WriteRequested(const GattWriteRequestedEventArgs &args,
                      std::shared_ptr<WriteDeferral> deferral);
  void Send(const std::shared_ptr<Link> &link);
};

void WindowsState::Handle(const std::string &method, const Map &args,
                          Response reply) {
  if (method == "start") {
    Start(Argument(args, "mode"), reply);
    return;
  }
  if (method == "stop") {
    Stop();
    reply->Ok();
    return;
  }
  if (method == "recover") {
    RadioChanged();
    reply->Ok();
    return;
  }
  if (method == "connect") {
    const auto peer = Argument(args, "peer");
    if (mode != "scan" || !peers.count(peer) ||
        (wanted.size() >= config.options.maximum_links &&
         !wanted.count(peer))) {
      reply->Error("peer_unavailable");
      return;
    }
    wanted.insert(peer);
    Connect(peer);
    reply->Ok();
    return;
  }
  if (method == "probe") {
    const auto peer = Argument(args, "peer"), token = Argument(args, "token");
    const bool probing =
        std::any_of(links.begin(), links.end(), [](const auto &item) {
          return !item.second->probe.empty();
        });
    if (mode != "scan" || !peers.count(peer) || wanted.count(peer) ||
        token.empty() || token.size() > 64 || ForPeer(peer) || probing ||
        links.size() >= config.options.maximum_links) {
      reply->Error("peer_unavailable");
      return;
    }
    Connect(peer, token);
    reply->Ok();
    return;
  }
  if (method == "cancelProbe" || method == "adoptProbe") {
    const auto token = Argument(args, "token");
    const auto probe =
        std::find_if(links.begin(), links.end(), [&](const auto &item) {
          return !token.empty() && item.second->probe == token;
        });
    const auto link = probe == links.end() ? nullptr : probe->second;
    if (method == "cancelProbe") {
      if (link)
        Drop(link);
    } else {
      const auto old = Argument(args, "oldPeer");
      const auto previous = ForPeer(old);
      if (!link || !link->ready ||
          (previous && previous != link && previous->ready)) {
        reply->Error("link_unavailable");
        return;
      }
      if (wanted.size() >= config.options.maximum_links && !wanted.count(old) &&
          !wanted.count(link->peer)) {
        reply->Error("peer_unavailable");
        return;
      }
      wanted.erase(old);
      retries.erase(old);
      if (previous && previous != link)
        Drop(previous);
      wanted.insert(link->peer);
      link->probe.clear();
    }
    reply->Ok();
    return;
  }
  const auto found = links.find(Argument(args, "link"));
  const auto link = found == links.end() ? nullptr : found->second;
  if (method == "disconnect" || method == "resetLink") {
    if (link)
      DisconnectLink(link, method == "resetLink");
    reply->Ok();
    return;
  }
  if (method == "send") {
    const auto bytes_it = args.find(Value("data"));
    const auto *bytes =
        bytes_it == args.end()
            ? nullptr
            : std::get_if<std::vector<uint8_t>>(&bytes_it->second);
    if (!link || !link->ready || link->sending || !bytes ||
        (bytes->empty() || bytes->size() > 65560)) {
      reply->Error("link_unavailable");
      return;
    }
    link->pending = *bytes;
    link->offset = 0;
    link->sending = reply;
    link->deadline = Clock::now() +
                     std::chrono::milliseconds(config.options.send_timeout_ms);
    Send(link);
    return;
  }
  reply->Error("invalid_operation");
}

void WindowsState::Drain() {
  std::deque<std::function<void()>> batch;
  bool flooded;
  {
    std::lock_guard<std::mutex> guard(queue_mutex);
    batch.swap(queue);
    flooded = std::exchange(overflow, false);
    wake_pending = false;
  }
  if (flooded) {
    Stop();
    dkble::Event e;
    e.type = DKBLE_EVENT_ERROR;
    e.status = DKBLE_OVERFLOW;
    e.generation = abi_generation;
    e.detail = "native-callback-overflow";
    emit(std::move(e));
    return;
  }
  for (auto &task : batch) {
    try {
      task();
    } catch (...) {
      Stop();
      Status(ErrorCode());
    }
  }
}

void WindowsState::Tick() {
  const auto now = Clock::now();
  // A radio/advertisement callback can arrive after the radio has already
  // come back. Recheck while suspended instead of depending on event order.
  if (waiting_for_radio && radio && radio.State() == RadioState::On &&
      now >= radio_retry_at)
    RadioChanged();
  if (starting && now >= start_deadline)
    StartFailed("unavailable");
  if (provider && advertising_deadline != Clock::time_point{}) {
    if (provider.AdvertisementStatus() ==
        GattServiceProviderAdvertisementStatus::Started)
      AdvertisementChanged(BluetoothError::Success);
    else if (now >= advertising_deadline)
      StartFailed("unavailable");
  }
  std::vector<std::shared_ptr<Link>> expired;
  for (const auto &entry : links)
    if ((!entry.second->ready || entry.second->sending) &&
        now >= entry.second->deadline)
      expired.push_back(entry.second);
  for (const auto &link : expired)
    Drop(link, true);
  std::vector<std::shared_ptr<Link>> polling;
  for (auto &[id, link] : links)
    if (link->polling)
      polling.push_back(link);
  for (auto &link : polling)
    Read(link);
  std::vector<Operation> timed_out;
  for (auto it = operations.begin(); it != operations.end();) {
    if (now >= it->second.deadline) {
      timed_out.push_back(std::move(it->second));
      it = operations.erase(it);
    } else
      ++it;
  }
  for (auto &operation : timed_out) {
    IgnoreFailure([&] { operation.async.Cancel(); });
    operation.failure("unavailable");
  }
  std::vector<std::string> retry;
  for (auto it = retries.begin(); it != retries.end();) {
    if (now >= it->second) {
      retry.push_back(it->first);
      it = retries.erase(it);
    } else
      ++it;
  }
  for (const auto &peer : retry)
    Connect(peer);
  for (auto it = peers.begin(); it != peers.end();) {
    if (now - it->second.seen >
            std::chrono::milliseconds(config.options.candidate_ttl_ms) &&
        !wanted.count(it->first) && !ForPeer(it->first)) {
      const auto peer = it->first;
      for (auto hint = hints.begin(); hint != hints.end();)
        if (hint->second == peer)
          hint = hints.erase(hint);
        else
          ++hint;
      it = peers.erase(it);
      Emit({{Value("type"), Value("candidateGone")},
            {Value("peer"), Value(peer)}});
    } else
      ++it;
  }
}

void WindowsState::DisconnectLink(const std::shared_ptr<Link> &link,
                                  bool retry) {
  if (!retry)
    wanted.erase(link->peer);
  // Rebuilding the peripheral service disconnects every subscriber. Only do
  // that for the last link; keep other ready or pending sessions intact.
  const bool republish = !link->initiator && retry && links.size() == 1;
  // Windows cannot force an individual peripheral-role disconnect. Refuse
  // further writes from this subscription until the peer unsubscribes.
  if (!link->initiator && !republish)
    rejected_subscribers.insert(link->device_key);
  Drop(link, retry);
  if (republish) {
    ClearTransport();
    BeginRole();
  }
}

void WindowsState::Drop(std::shared_ptr<Link> link, bool retry) {
  if (!Alive(link))
    return;
  links.erase(link->id);
  Cancel(&link->id);
  if (link->sending) {
    link->sending->Error("link_closed");
    link->sending.reset();
  }
  if (link->notify && link->notify_token.value)
    IgnoreFailure([&] { link->notify.ValueChanged(link->notify_token); });
  if (link->device) {
    if (link->connection_token.value)
      IgnoreFailure([&] {
        link->device.ConnectionStatusChanged(link->connection_token);
      });
    if (link->services_token.value)
      IgnoreFailure(
          [&] { link->device.GattServicesChanged(link->services_token); });
  }
  if (link->session) {
    if (link->session_token.value)
      IgnoreFailure(
          [&] { link->session.SessionStatusChanged(link->session_token); });
    IgnoreFailure([&] { link->session.MaintainConnection(false); });
    IgnoreFailure([&] { link->session.Close(); });
  }
  if (link->service)
    IgnoreFailure([&] { link->service.Close(); });
  if (link->device)
    IgnoreFailure([&] { link->device.Close(); });
  link->pending.clear();
  link->early.clear();
  link->notify = nullptr;
  link->write = nullptr;
  link->subscriber = nullptr;
  link->session = nullptr;
  link->service = nullptr;
  link->device = nullptr;
  if (link->ready)
    Emit({{Value("type"), Value("disconnected")},
          {Value("link"), Value(link->id)}});
  if (!link->probe.empty()) {
    // A probe never owns persistent retries, even if a late public hint or a
    // future caller accidentally associated its address with wanted.
    wanted.erase(link->peer);
    retries.erase(link->peer);
    Emit({{Value("type"), Value("probeEnded")},
          {Value("token"), Value(link->probe)}});
  } else if (retry && mode == "scan" && wanted.count(link->peer))
    retries[link->peer] =
        Clock::now() + std::chrono::milliseconds(config.options.retry_delay_ms);
}

void WindowsState::ClearTransport() {
  if (watcher) {
    IgnoreFailure([&] {
      watcher.Received(received_token);
      watcher.Stopped(stopped_token);
      watcher.Stop();
    });
    watcher = nullptr;
  }
  while (!links.empty())
    Drop(links.begin()->second, true);
  if (provider) {
    IgnoreFailure([&] {
      provider.AdvertisementStatusChanged(advertising_token);
      provider.StopAdvertising();
    });
  }
  if (local_rx)
    IgnoreFailure([&] { local_rx.WriteRequested(write_token); });
  if (local_tx)
    IgnoreFailure(
        [&] { local_tx.SubscribedClientsChanged(subscribers_token); });
  local_rx = nullptr;
  local_tx = nullptr;
  provider = nullptr;
  publishing = false;
  advertising_deadline = {};
  rejected_subscribers.clear();
  const std::string server_owner = "server";
  Cancel(&server_owner);
}

void WindowsState::Stop() {
  ++generation;
  mode = "off";
  waiting_for_radio = false;
  radio_recovery_deadline = {};
  radio_retry_at = {};
  if (starting) {
    starting->Error("link_closed");
    starting.reset();
  }
  ClearTransport();
  if (radio && radio_token.value)
    IgnoreFailure([&] { radio.StateChanged(radio_token); });
  radio_token = {};
  radio = nullptr;
  adapter = nullptr;
  Cancel();
  wanted.clear();
  retries.clear();
  peers.clear();
  hints.clear();
  Status("off");
}

void WindowsState::Close() {
  {
    std::lock_guard<std::mutex> guard(queue_mutex);
    closed = true;
    queue.clear();
  }
  Stop();
}

void WindowsState::Start(const std::string &selected, Response reply) {
  if (selected != "scan" && selected != "advertise") {
    reply->Error("invalid_mode");
    return;
  }
  Stop();
  mode = selected;
  starting = reply;
  start_deadline = Clock::now() + 20s;
  Status("starting");
  const auto epoch = generation;
  Await(
      BluetoothAdapter::GetDefaultAsync(),
      [this, epoch](const BluetoothAdapter &found) {
        adapter = found;
        if (!adapter || !adapter.IsLowEnergySupported() ||
            (mode == "scan" && !adapter.IsCentralRoleSupported()) ||
            (mode == "advertise" && !adapter.IsPeripheralRoleSupported())) {
          StartFailed("unsupported");
          return;
        }
        Await(
            adapter.GetRadioAsync(),
            [this, epoch](const Radio &found_radio) {
              radio = found_radio;
              if (!radio || radio.State() != RadioState::On) {
                StartFailed("bluetooth_off");
                return;
              }
              const auto weak = weak_from_this();
              radio_token = radio.StateChanged(
                  [weak, epoch](const Radio &source, const auto &) {
                    Dispatch(weak, epoch, [source](State &self) {
                      if (self.radio == source)
                        self.RadioChanged();
                    });
                  });
              BeginRole();
            },
            [this](const auto &code) { StartFailed(code); });
      },
      [this](const auto &code) { StartFailed(code); });
}

void WindowsState::StartFailed(const std::string &code) {
  if (code == "link_closed")
    return; // Explicit stop already completes its caller.
  if (!starting && mode != "off" && radio &&
      (code == "bluetooth_off" || radio.State() != RadioState::On)) {
    WaitForRadio();
    return;
  }
  // RadioState::On can precede actual adapter readiness. Keep the enabled role
  // during a bounded recovery window, including synchronous watcher failures.
  if (!starting && waiting_for_radio && code == "unavailable" &&
      Clock::now() < radio_recovery_deadline) {
    ClearTransport();
    radio_retry_at = Clock::now() + 500ms;
    Status("starting");
    return;
  }
  auto reply = std::move(starting);
  Stop();
  Status(code);
  if (reply)
    reply->Error(code);
}

void WindowsState::BeginRole() {
  try {
    if (mode == "scan")
      Scan();
    else if (mode == "advertise" && !provider && !publishing)
      Publish();
  } catch (...) {
    StartFailed(ErrorCode());
  }
}

void WindowsState::RadioChanged() {
  if (mode == "off" || !radio)
    return;
  if (radio.State() != RadioState::On) {
    WaitForRadio();
    return;
  }
  if (waiting_for_radio) {
    const auto now = Clock::now();
    if (radio_recovery_deadline == Clock::time_point{})
      radio_recovery_deadline = now + 10s;
    if (now >= radio_recovery_deadline) {
      StartFailed("unavailable");
      return;
    }
    if (now < radio_retry_at)
      return;
    radio_retry_at = now + 500ms;
  }
  BeginRole();
  if (!waiting_for_radio)
    for (const auto &peer : wanted)
      Connect(peer);
}

void WindowsState::WaitForRadio() {
  if (mode == "off")
    return;
  if (starting) {
    // The initial start still reports a failure if it never became usable.
    auto reply = std::move(starting);
    Stop();
    Status("bluetooth_off");
    reply->Error("bluetooth_off");
    return;
  }
  waiting_for_radio = true;
  if (radio && radio.State() != RadioState::On) {
    radio_recovery_deadline = {};
    radio_retry_at = {};
  }
  ClearTransport();
  Status("bluetooth_off");
}

void WindowsState::Scan() {
  if (watcher)
    return;
  watcher = BluetoothLEAdvertisementWatcher();
  watcher.ScanningMode(BluetoothLEScanningMode::Active);
  const auto weak = weak_from_this();
  const auto epoch = generation;
  received_token = watcher.Received(
      [weak, epoch, kService = kService](
          const auto &source,
          const BluetoothLEAdvertisementReceivedEventArgs &args) {
        try {
          bool matches = false;
          for (const auto &uuid : args.Advertisement().ServiceUuids())
            if (uuid == kService)
              matches = true;
          std::string hint;
          // Windows can report an Android scan response separately from its
          // advertisement. Match the service in either packet, before queueing.
          for (const auto &section : args.Advertisement().DataSections())
            if (section.DataType() == 0x21) {
              auto candidate =
                  AdvertisementHint(Bytes(section.Data()), kService);
              if (!candidate.empty()) {
                hint = std::move(candidate);
                matches = true;
              }
            }
          if (!matches)
            return;
          const auto address = args.BluetoothAddress();
          const auto type = args.BluetoothAddressType();
          const auto rssi = args.RawSignalStrengthInDBm();
          Dispatch(weak, epoch,
                   [source, address, type, rssi, hint](State &self) {
                     if (self.watcher == source)
                       self.Discovered(address, type, rssi, hint);
                   });
        } catch (...) {
        }
      });
  stopped_token =
      watcher.Stopped([weak, epoch](const auto &source, const auto &args) {
        const auto error = args.Error();
        Dispatch(weak, epoch, [source, error](State &self) {
          if (self.watcher != source)
            return;
          if (error == BluetoothError::RadioNotAvailable ||
              (self.radio && self.radio.State() != RadioState::On))
            self.WaitForRadio();
          else
            self.StartFailed("unavailable");
        });
      });
  watcher.Start();
  waiting_for_radio = false;
  radio_recovery_deadline = {};
  Status("scanning");
  if (starting) {
    starting->Ok();
    starting.reset();
  }
}

void WindowsState::Discovered(uint64_t address, BluetoothAddressType type,
                              int rssi, const std::string &hint) {
  if (mode != "scan")
    return;
  const auto peer =
      std::to_string(address) + ":" + std::to_string(static_cast<int>(type));
  if (!peers.count(peer) && peers.size() >= config.options.maximum_candidates)
    return;
  peers.insert_or_assign(peer, Peer{address, type, Clock::now()});
  if (config.hints && !hint.empty()) {
    const auto previous = hints.find(hint);
    if (previous != hints.end()) {
      const auto old = previous->second;
      const auto link = ForPeer(old);
      const auto target = ForPeer(peer);
      // Only authenticated adoption can promote a probe to automatic retry.
      // A matching public hint must not bypass consumer identity verification.
      if (nearby_ble::CanMoveHint(old, peer, link && link->ready) &&
          (!target || target->probe.empty())) {
        const bool reconnect = wanted.erase(old) != 0;
        retries.erase(old);
        if (link)
          Drop(link);
        peers.erase(old);
        Emit({{Value("type"), Value("candidateGone")},
              {Value("peer"), Value(old)}});
        previous->second = peer;
        if (reconnect)
          wanted.insert(peer);
      }
    } else if (hints.size() < config.options.maximum_candidates)
      hints.emplace(hint, peer);
  }
  Emit({{Value("type"), Value("candidate")},
        {Value("peer"), Value(peer)},
        {Value("rssi"), Value(rssi)}});
  if (wanted.count(peer))
    Connect(peer);
}

void WindowsState::Connect(const std::string &peer, const std::string &probe) {
  if (mode != "scan" || !radio || radio.State() != RadioState::On ||
      (probe.empty() && !wanted.count(peer)) || !peers.count(peer) ||
      ForPeer(peer) || links.size() >= config.options.maximum_links ||
      retries.count(peer))
    return;
  const auto candidate = peers.at(peer);
  auto link = std::make_shared<Link>();
  link->deadline = Clock::now() +
                   std::chrono::milliseconds(config.options.connect_timeout_ms);
  link->peer = peer;
  link->probe = probe;
  link->initiator = true;
  links.emplace(link->id, link);
  const auto weak = weak_from_this();
  const auto epoch = generation;
  auto failure = [this, link](const std::string &) { Drop(link, true); };
  try {
    // Start on the owner thread: Windows may need device access consent.
    Await(
        BluetoothLEDevice::FromBluetoothAddressAsync(candidate.address,
                                                     candidate.type),
        [this, weak, epoch, link, failure](const BluetoothLEDevice &device) {
          if (!device) {
            Drop(link, true);
            return;
          }
          link->device = device;
          link->connection_token = device.ConnectionStatusChanged(
              [weak, epoch, link](const auto &, const auto &) {
                Dispatch(weak, epoch, [link](State &self) {
                  if (self.Alive(link) && link->ready &&
                      link->device.ConnectionStatus() ==
                          BluetoothConnectionStatus::Disconnected)
                    self.Drop(link, true);
                });
              });
          link->services_token = device.GattServicesChanged(
              [weak, epoch, link](const auto &, const auto &) {
                Dispatch(weak, epoch,
                         [link](State &self) { self.Drop(link, true); });
              });
          Await(
              device.GetGattServicesForUuidAsync(kService,
                                                 BluetoothCacheMode::Uncached),
              [this, weak, epoch, link,
               failure](const GattDeviceServicesResult &result) {
                if (result.Status() != GattCommunicationStatus::Success ||
                    result.Services().Size() != 1) {
                  Drop(link, true);
                  return;
                }
                link->service = result.Services().GetAt(0);
                link->session = link->service.Session();
                if (link->session.CanMaintainConnection())
                  link->session.MaintainConnection(true);
                Await(
                    link->service.GetCharacteristicsAsync(
                        BluetoothCacheMode::Uncached),
                    [this, weak, epoch, link, failure](
                        const GattCharacteristicsResult &characteristics) {
                      if (characteristics.Status() !=
                          GattCommunicationStatus::Success) {
                        Drop(link, true);
                        return;
                      }
                      for (const auto &characteristic :
                           characteristics.Characteristics()) {
                        if (characteristic.Uuid() == kRx &&
                            (characteristic.CharacteristicProperties() &
                             GattCharacteristicProperties::Write) !=
                                GattCharacteristicProperties::None)
                          link->write = characteristic;
                        if (characteristic.Uuid() == kTx &&
                            (characteristic.CharacteristicProperties() &
                             (GattCharacteristicProperties::Notify |
                              GattCharacteristicProperties::Read)) !=
                                GattCharacteristicProperties::None) {
                          link->notify = characteristic;
                          link->polling =
                              (characteristic.CharacteristicProperties() &
                               GattCharacteristicProperties::Notify) ==
                              GattCharacteristicProperties::None;
                        }
                      }
                      if (!link->write || !link->notify) {
                        Drop(link, true);
                        return;
                      }
                      if (link->polling) {
                        BeginReadSession(link);
                        return;
                      }
                      link->notify_token = link->notify.ValueChanged(
                          [weak, epoch,
                           link](const auto &,
                                 const GattValueChangedEventArgs &args) {
                            try {
                              auto bytes = Bytes(args.CharacteristicValue());
                              Dispatch(weak, epoch,
                                       [link,
                                        bytes = std::move(bytes)](State &self) {
                                         self.Receive(link, bytes);
                                       });
                            } catch (...) {
                              Dispatch(weak, epoch, [link](State &self) {
                                self.Drop(link, true);
                              });
                            }
                          });
                      Await(
                          link->notify
                              .WriteClientCharacteristicConfigurationDescriptorAsync(
                                  GattClientCharacteristicConfigurationDescriptorValue::
                                      Notify),
                          [this, link](GattCommunicationStatus status) {
                            if (status != GattCommunicationStatus::Success) {
                              Drop(link, true);
                              return;
                            }
                            Ready(link);
                          },
                          failure, link->id);
                    },
                    failure, link->id);
              },
              failure, link->id);
        },
        failure, link->id);
  } catch (...) {
    Drop(link, true);
  }
}

void WindowsState::BeginReadSession(const std::shared_ptr<Link> &link) {
  try {
    GUID id{};
    check_hresult(CoCreateGuid(&id));
    std::copy_n(id.Data4, link->epoch.size(), link->epoch.begin());
    auto packet = read_session::packet(read_session::hello, link->epoch);
    DataWriter writer;
    writer.WriteBytes(packet);
    link->writing = true;
    Await(link->write.WriteValueWithResultAsync(
              writer.DetachBuffer(), GattWriteOption::WriteWithResponse),
          [this, link](const GattWriteResult &result) {
            if (!Alive(link)) return;
            link->writing = false;
            if (result.Status() != GattCommunicationStatus::Success) {
              Drop(link, true); return;
            }
            link->helloSent = true;
            Read(link);
          }, [this, link](const std::string &) { Drop(link, true); }, link->id);
  } catch (...) { Drop(link, true); }
}
void WindowsState::Read(const std::shared_ptr<Link> &link) {
  if (!Alive(link) || !link->polling || !link->helloSent || link->reading || link->writing ||
      Clock::now() < link->read_at)
    return;
  link->reading = true;
  try {
    Await(
        link->notify.ReadValueAsync(BluetoothCacheMode::Uncached),
        [this, link](const GattReadResult &result) {
          if (!Alive(link))
            return;
          link->reading = false;
          link->read_at =
              Clock::now() + std::chrono::milliseconds(config.readInterval);
          if (result.Status() != GattCommunicationStatus::Success) {
            Drop(link, true);
            return;
          }
          auto data = Bytes(result.Value());
          uint8_t kind;
          read_session::Epoch epoch;
          if (!read_session::parse(data, kind, epoch) || kind > 1) {
            Drop(link, true);
            return;
          }
          if (epoch != link->epoch) {
            dkble::Event e;
            e.type = DKBLE_EVENT_DIAGNOSTIC;
            e.generation = abi_generation;
            e.detail = "stale-read-epoch";
            emit(std::move(e));
            return;
          }
          data.erase(data.begin(), data.begin() + read_session::header);
          if (!data.empty())
            link->read_at = Clock::now();
          Ready(link);
          if (!data.empty())
            Receive(link, std::move(data));
          if (link->sending)
            Send(link);
        },
        [this, link](const std::string &) { Drop(link, true); }, link->id);
  } catch (...) {
    Drop(link, true);
  }
}
void WindowsState::Ready(const std::shared_ptr<Link> &link) {
  if (!Alive(link) || link->ready)
    return;
  link->ready = true;
  Emit({{Value("type"), Value("link")},
        {Value("link"), Value(link->id)},
        {Value("peer"), Value(link->peer)},
        {Value("initiator"), Value(link->initiator)},
        {Value("probe"), Value(link->probe)}});
  auto early = std::move(link->early);
  link->early_bytes = 0;
  for (auto &bytes : early)
    Receive(link, std::move(bytes));
}

void WindowsState::Receive(const std::shared_ptr<Link> &link,
                           std::vector<uint8_t> bytes) {
  if (!Alive(link))
    return;
  if (bytes.empty() || bytes.size() > 512) {
    Drop(link, true);
    return;
  }
  if (!link->ready) {
    // A first notification can race completion of the CCCD write.
    if (link->early_bytes + bytes.size() > 4096 || link->early.size() >= 128) {
      Drop(link, true);
      return;
    }
    link->early_bytes += bytes.size();
    link->early.push_back(std::move(bytes));
    return;
  }
  Emit({{Value("type"), Value("data")},
        {Value("link"), Value(link->id)},
        {Value("data"), Value(std::move(bytes))}});
}

void WindowsState::Publish() {
  publishing = true;
  const auto weak = weak_from_this();
  const auto epoch = generation;
  auto failure = [this](const std::string &code) { StartFailed(code); };
  Await(
      GattServiceProvider::CreateAsync(kService),
      [this, weak, epoch, failure](const GattServiceProviderResult &result) {
        Trace("create service", static_cast<int32_t>(result.Error()));
        if (result.Error() != BluetoothError::Success) {
          StartFailed(BluetoothFailure(result.Error()));
          return;
        }
        provider = result.ServiceProvider();
        GattLocalCharacteristicParameters rx_parameters;
        rx_parameters.CharacteristicProperties(
            GattCharacteristicProperties::Write);
        rx_parameters.WriteProtectionLevel(GattProtectionLevel::Plain);
        Await(
            provider.Service().CreateCharacteristicAsync(kRx, rx_parameters),
            [this, weak, epoch,
             failure](const GattLocalCharacteristicResult &rx_result) {
              Trace("create rx", static_cast<int32_t>(rx_result.Error()));
              if (rx_result.Error() != BluetoothError::Success) {
                StartFailed(BluetoothFailure(rx_result.Error()));
                return;
              }
              local_rx = rx_result.Characteristic();
              GattLocalCharacteristicParameters tx_parameters;
              tx_parameters.CharacteristicProperties(
                  GattCharacteristicProperties::Notify);
              tx_parameters.ReadProtectionLevel(GattProtectionLevel::Plain);
              Await(
                  provider.Service().CreateCharacteristicAsync(kTx,
                                                               tx_parameters),
                  [this, weak,
                   epoch](const GattLocalCharacteristicResult &tx_result) {
                    Trace("create tx", static_cast<int32_t>(tx_result.Error()));
                    if (tx_result.Error() != BluetoothError::Success) {
                      StartFailed(BluetoothFailure(tx_result.Error()));
                      return;
                    }
                    local_tx = tx_result.Characteristic();
                    write_token = local_rx.WriteRequested(
                        [weak, epoch](const auto &source,
                                      const GattWriteRequestedEventArgs &args) {
                          try {
                            auto deferral = std::make_shared<WriteDeferral>(
                                args.GetDeferral());
                            Dispatch(weak, epoch,
                                     [source, args, deferral](State &self) {
                                       if (self.local_rx == source)
                                         self.WriteRequested(args, deferral);
                                     });
                          } catch (...) {
                          }
                        });
                    subscribers_token = local_tx.SubscribedClientsChanged(
                        [weak, epoch](const auto &source, const auto &) {
                          Dispatch(weak, epoch, [source](State &self) {
                            if (self.local_tx == source)
                              self.Subscribers();
                          });
                        });
                    advertising_token = provider.AdvertisementStatusChanged(
                        [weak, epoch](const auto &source, const auto &args) {
                          const auto status = args.Status();
                          Trace("advertisement status",
                                static_cast<int32_t>(status));
                          Trace("advertisement error",
                                static_cast<int32_t>(args.Error()));
                          const auto error = args.Error();
                          Dispatch(weak, epoch, [source, error](State &self) {
                            if (self.provider != source)
                              return;
                            self.AdvertisementChanged(error);
                          });
                        });
                    GattServiceProviderAdvertisingParameters parameters;
                    parameters.IsConnectable(true);
                    parameters.IsDiscoverable(true);
                    provider.StartAdvertising(parameters);
                    publishing = false;
                  },
                  failure, "server");
            },
            failure, "server");
      },
      failure, "server");
}

void WindowsState::AdvertisementChanged(BluetoothError error) {
  if (!provider || mode != "advertise")
    return;
  if (error == BluetoothError::RadioNotAvailable ||
      (radio && radio.State() != RadioState::On)) {
    WaitForRadio();
    return;
  }
  // Read the current status on the owner thread. Intel adapters can report
  // Aborted/Success immediately followed by Started while registering a
  // service; treating that transient event as final cancels a valid start.
  const auto status = provider.AdvertisementStatus();
  if (status == GattServiceProviderAdvertisementStatus::Started) {
    advertising_deadline = {};
    waiting_for_radio = false;
    radio_recovery_deadline = {};
    Status("advertising");
    if (starting) {
      starting->Ok();
      starting.reset();
    }
  } else if (error != BluetoothError::Success ||
             status == GattServiceProviderAdvertisementStatus::
                           StartedWithoutAllAdvertisementData) {
    // Incomplete advertisement data may omit our service UUID.
    StartFailed("unavailable");
  } else {
    Status("starting");
    if (advertising_deadline == Clock::time_point{})
      advertising_deadline = Clock::now() + std::chrono::milliseconds(
                                                config.options.retry_delay_ms);
  }
}

void WindowsState::Subscribers() {
  if (mode != "advertise" || !local_tx)
    return;
  std::set<std::string> current;
  const auto weak = weak_from_this();
  const auto epoch = generation;
  for (const auto &subscriber : local_tx.SubscribedClients()) {
    const auto session = subscriber.Session();
    const auto key = to_string(session.DeviceId().Id());
    current.insert(key);
    if (rejected_subscribers.count(key))
      continue;
    bool exists = false;
    for (const auto &entry : links)
      if (!entry.second->initiator && entry.second->device_key == key)
        exists = true;
    if (exists)
      continue;
    if (links.size() >= config.options.maximum_links ||
        rejected_subscribers.size() >= config.options.maximum_candidates) {
      continue;
    }
    auto link = std::make_shared<Link>();
    link->peer = link->id;
    link->device_key = key;
    link->subscriber = subscriber;
    link->session = session;
    links.emplace(link->id, link);
    link->session_token = session.SessionStatusChanged(
        [weak, epoch, link](const auto &, const auto &args) {
          const auto status = args.Status();
          Dispatch(weak, epoch, [link, status](State &self) {
            if (status == GattSessionStatus::Closed)
              self.Drop(link);
          });
        });
    // Unsubscribed inbound devices consume no application link slots.
    Ready(link);
  }
  std::vector<std::shared_ptr<Link>> removed;
  for (const auto &entry : links)
    if (!entry.second->initiator && !current.count(entry.second->device_key))
      removed.push_back(entry.second);
  for (const auto &link : removed)
    Drop(link);
  for (auto it = rejected_subscribers.begin();
       it != rejected_subscribers.end();) {
    if (!current.count(*it))
      it = rejected_subscribers.erase(it);
    else
      ++it;
  }
}

void WindowsState::WriteRequested(const GattWriteRequestedEventArgs &args,
                                  std::shared_ptr<WriteDeferral> deferral) {
  const auto key = to_string(args.Session().DeviceId().Id());
  Await(
      args.GetRequestAsync(),
      [this, key, deferral](const GattWriteRequest &request) {
        if (!request)
          return;
        Subscribers(); // Subscription and the first write may arrive together.
        std::shared_ptr<Link> link;
        for (const auto &entry : links)
          if (!entry.second->initiator && entry.second->device_key == key)
            link = entry.second;
        auto bytes = Bytes(request.Value());
        if (!link || !link->ready || request.Offset() != 0 || bytes.empty() ||
            request.Option() != GattWriteOption::WriteWithResponse) {
          request.RespondWithProtocolError(0x0e);
          return;
        }
        Receive(link, std::move(bytes));
        request.Respond();
      },
      [deferral](const auto &) {}, "server");
}

void WindowsState::Send(const std::shared_ptr<Link> &link) {
  if (!Alive(link) || !link->sending || link->reading || link->writing)
    return;
  if (link->offset == link->pending.size()) {
    link->pending.clear();
    auto reply = std::move(link->sending);
    reply->Ok();
    return;
  }
  if (link->polling && Clock::now() >= link->read_at) {
    Read(link);
    return;
  }
  try {
    auto count = nearby_ble::FragmentSize(link->session.MaxPduSize(),
                                          link->pending.size() - link->offset);
    if (link->polling)
      count = std::min(count, size_t{20} - read_session::header);
    if (!link->initiator)
      count = std::min(
          count, static_cast<size_t>(link->subscriber.MaxNotificationSize()));
    if (count == 0) {
      Drop(link, true);
      return;
    }
    DataWriter writer;
    auto slice = std::span(link->pending).subspan(link->offset, count);
    if (link->polling)
      writer.WriteBytes(read_session::packet(read_session::write, link->epoch, slice));
    else
      writer.WriteBytes(array_view<const uint8_t>(slice.data(), slice.data() + slice.size()));
    link->writing = true;
    auto completed = [this, link, count](const auto &result) {
      if (!Alive(link))
        return;
      link->writing = false;
      if (result.Status() != GattCommunicationStatus::Success) {
        dkble::Event e;
        e.generation = abi_generation;
        e.type = DKBLE_EVENT_DIAGNOSTIC;
        e.detail = "gatt-write-status:" +
                   std::to_string(static_cast<int>(result.Status()));
        if constexpr (requires { result.ProtocolError(); }) {
          auto protocol = result.ProtocolError();
          if (protocol)
            e.detail += " protocol=" + std::to_string(protocol.Value());
        }
        emit(std::move(e));
        Drop(link, true);
        return;
      }
      link->offset += count;
      Send(link);
    };
    auto failed = [this, link](const std::string &code) {
      dkble::Event e;
      e.generation = abi_generation;
      e.type = DKBLE_EVENT_DIAGNOSTIC;
      e.detail = "gatt-write-error:" + code;
      emit(std::move(e));
      Drop(link, true);
    };
    if (link->initiator)
      Await(link->write.WriteValueWithResultAsync(
                writer.DetachBuffer(), GattWriteOption::WriteWithResponse),
            completed, failed, link->id);
    else
      Await(local_tx.NotifyValueAsync(writer.DetachBuffer(), link->subscriber),
            completed, failed, link->id);
  } catch (...) {
    Drop(link, true);
  }
}

namespace dkble {
class WindowsBackend final : public Backend {
  std::shared_ptr<WindowsState> state;

public:
  WindowsBackend(Config config, Emit emit, Wake wake) {
    // Respect the caller's existing COM apartment; no radio access at create.
    auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE)
      check_hresult(result);
    initialized = SUCCEEDED(result);
    state = std::make_shared<WindowsState>(std::move(config), std::move(emit),
                                           std::move(wake));
  }
  bool initialized = false;
  ~WindowsBackend() override {
    close();
    if (initialized)
      CoUninitialize();
  }
  int set_read_interval(uint32_t interval) override {
    state->config.readInterval = interval;
    return 0;
  }
  int set_options(const dkble_options &options) override {
    state->config.options = options;
    return 0;
  }
  void poll() override {
    if (state) {
      state->Drain();
      state->Tick();
    }
  }
  void close() override {
    if (state) {
      state->Close();
      state.reset();
    }
  }
  int command(Command c) override {
    if (!state)
      return DKBLE_STATE;
    state->abi_generation = c.generation;
    Map args;
    std::string method;
    switch (c.op) {
    case Op::startScan:
      method = "start";
      args[Value("mode")] = Value("scan");
      break;
    case Op::startAdvertise:
      method = "start";
      args[Value("mode")] = Value("advertise");
      break;
    case Op::stop:
      method = "stop";
      break;
    case Op::connect:
      method = "connect";
      args[Value("peer")] = c.value;
      break;
    case Op::probe:
      method = "probe";
      args[Value("peer")] = c.value;
      args[Value("token")] = c.extra;
      break;
    case Op::cancel:
      method = "cancelProbe";
      args[Value("token")] = c.value;
      break;
    case Op::adopt:
      method = "adoptProbe";
      args[Value("token")] = c.value;
      args[Value("oldPeer")] = c.extra;
      break;
    case Op::disconnect:
      method = "disconnect";
      args[Value("link")] = c.value;
      break;
    case Op::reset:
      method = "resetLink";
      args[Value("link")] = c.value;
      break;
    case Op::send:
      method = "send";
      args[Value("link")] = c.value;
      args[Value("data")] = std::move(c.data);
      break;
    case Op::recover:
      method = "recover";
      break;
    }
    struct Result {
      bool synchronous = true;
      int status = 0;
    };
    auto result = std::make_shared<Result>();
    auto weak = std::weak_ptr<WindowsState>(state);
    auto reply = std::make_shared<Reply>();
    reply->completion = [weak, c, result](const std::string &error) {
      int code = error.empty()                  ? 0
                 : error == "unsupported"       ? DKBLE_UNSUPPORTED
                 : error == "invalid_operation" ? DKBLE_INVALID
                 : error == "peer_unavailable" || error == "link_unavailable"
                     ? DKBLE_STATE
                     : DKBLE_IO;
      if (result->synchronous && code) {
        result->status = code;
        return;
      }
      if (auto s = weak.lock(); s && s->abi_generation == c.generation) {
        if (c.op == Op::send || code) {
          Event e;
          e.type =
              c.op == Op::send ? DKBLE_EVENT_SEND_COMPLETE : DKBLE_EVENT_ERROR;
          e.generation = c.generation;
          e.request = c.request;
          e.link = c.value;
          e.status = code;
          e.detail = error;
          s->emit(std::move(e));
        }
      }
    };
    try {
      state->Handle(method, args, reply);
    } catch (...) {
      reply->Error(ErrorCode());
    }
    result->synchronous = false;
    return result->status;
  }
};
std::unique_ptr<Backend> make_backend(Config c, Emit e, Wake w) {
  return std::make_unique<WindowsBackend>(std::move(c), std::move(e),
                                          std::move(w));
}
} // namespace dkble
