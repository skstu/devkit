#include "libnet_ice_transport.h"
#include "libnet_uv.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace libnet {
namespace {
using Clock = std::chrono::steady_clock;
std::string Key(const NetworkPath &path) {
  return path.interface_name + "/" + path.local_address + "/" + std::to_string(path.android_network);
}
bool Connected(const IceAgent &agent) {
  return (agent.state() == IceState::connected || agent.state() == IceState::completed) &&
         agent.SelectedPathAllowed();
}
std::string RandomCredential(std::size_t size) {
  unsigned char bytes[32]{};
  if (size > sizeof(bytes) || RAND_bytes(bytes, static_cast<int>(size)) != 1) return {};
  std::string text;
  for (std::size_t i = 0; i < size; ++i) {
    text += "0123456789abcdef"[bytes[i] >> 4];
    text += "0123456789abcdef"[bytes[i] & 15];
  }
  OPENSSL_cleanse(bytes, sizeof(bytes));
  return text;
}
void AddCounts(IceCandidateCounts &a, const IceCandidateCounts &b) {
  a.host += b.host; a.srflx += b.srflx; a.prflx += b.prflx; a.relay += b.relay;
  a.ipv4 += b.ipv4; a.ipv6 += b.ipv6; a.global_ipv4 += b.global_ipv4;
  a.global_ipv6 += b.global_ipv6; a.link_local += b.link_local;
  a.private_ipv4 += b.private_ipv4; a.unique_local_ipv6 += b.unique_local_ipv6; a.other += b.other;
}
} // namespace

struct IceTransport::Impl {
  mutable std::recursive_mutex mutex;
  IceConfig config;
  std::unique_ptr<IceAgent> single;
  std::unique_ptr<TcpDirectAgent> tcp;
  struct Slot {
    NetworkPath path;
    std::unique_ptr<IceAgent> agent;
    unsigned attempts = 0;
    Clock::time_point retry_at{};
    Clock::time_point received_at{};
  };
  std::vector<Slot> slots;
  std::vector<NetworkPath> inventory;
  std::string ufrag, password, remote;
  bool running = false, gathering = false, changed = false;
  bool ever_connected = false;
  bool recovering = false;
  mutable IceAgent *selected = nullptr;
  mutable std::uint64_t switches = 0;
  IceState emitted = IceState::disconnected;
  std::string emitted_path;
  Clock::time_point began{}, recovery_deadline{};
  std::uint64_t revision = 0, attempts = 0;

  IceAgent *Choose() const {
    if (single) return single.get();
    if (selected && Connected(*selected)) {
      Clock::time_point selected_received{};
      const Slot *recent = nullptr;
      for (const auto &slot : slots) {
        if (slot.agent.get() == selected) selected_received = slot.received_at;
        if (slot.agent && Connected(*slot.agent) && (!recent || slot.received_at > recent->received_at)) recent = &slot;
      }
      if (recent && recent->agent.get() != selected && recent->received_at > selected_received &&
          Clock::now() - selected_received > std::chrono::milliseconds(500)) {
        selected = recent->agent.get(); ++switches;
      }
      return selected;
    }
    const auto previous = selected;
    selected = nullptr;
    for (const auto &slot : slots) {
      if (slot.agent && Connected(*slot.agent)) { selected = slot.agent.get(); break; }
    }
    if (selected && selected != previous) ++switches;
    return selected;
  }
  IceAgent *Inspect() const {
    if (auto *agent = Choose()) return agent;
    for (const auto &slot : slots) if (slot.agent) return slot.agent.get();
    return nullptr;
  }
  bool Compatible(const NetworkPath &path) const {
    const auto source = Endpoint::Parse(path.local_address, 0);
    return source && (config.address_policy == IceAddressPolicy::automatic ||
        (source->family() == AddressFamily::ipv4) == (config.address_policy == IceAddressPolicy::ipv4));
  }
  bool Build(Slot &slot) {
    ++slot.attempts; ++attempts;
    slot.retry_at = Clock::now() + std::chrono::seconds(1 << slot.attempts);
    auto child = config;
    child.manage_network_paths = false;
    child.network_paths.clear();
    child.network_path = slot.path;
    const auto source = Endpoint::Parse(slot.path.local_address, 0);
    if (!source) return false;
    child.address_policy = source->family() == AddressFamily::ipv4 ? IceAddressPolicy::ipv4 : IceAddressPolicy::ipv6;
    if (!child.stun_host.empty()) {
      const auto server = Endpoint::Parse(child.stun_host, child.stun_port);
      if (!server) return false;
      // A numeric v4 STUN cannot serve a v6 socket. That socket still tries
      // its directly reachable host addresses, without off-path DNS.
      if (server->family() != source->family()) child.stun_host.clear();
    }
    auto agent = std::make_unique<IceAgent>();
    if (!agent->Start(child) || !agent->SetLocalCredentials(ufrag, password) ||
        (!remote.empty() && !agent->SetRemoteDescription(remote)) ||
        (gathering && !agent->BeginGather())) return false;
    slot.agent = std::move(agent);
    return true;
  }
  void Refresh(const std::vector<NetworkPath> &paths) {
    std::vector<NetworkPath> desired;
    std::set<std::string> unique;
    for (const auto &path : paths) {
      if (Compatible(path) && unique.insert(Key(path)).second) desired.push_back(path);
      if (desired.size() == 4) break;
    }
    std::set<std::string> before, after;
    for (const auto &path : inventory) before.insert(Key(path));
    for (const auto &path : desired) after.insert(Key(path));
    if (before == after) return;
    inventory = desired;
    ++revision; changed = true;
    recovery_deadline = Clock::now() + std::chrono::seconds(30);
    std::erase_if(slots, [&](auto &slot) {
      if (after.contains(Key(slot.path))) return false;
      if (selected == slot.agent.get()) selected = nullptr;
      return true;
    });
    for (const auto &path : desired) {
      if (before.contains(Key(path))) continue;
      // Do not impersonate an ICE restart by recycling credentials after
      // signaling. The owner negotiates a new generation over a live bearer.
      if (!remote.empty()) continue;
      slots.push_back({path, {}, 0, {}, {}});
      Build(slots.back());
    }
  }
  void Tick() {
    if (!running || single || !gathering) return;
    const auto now = Clock::now();
    if (Choose()) recovering = false;
    else if (ever_connected && !recovering) {
      recovering = true;
      recovery_deadline = now + std::chrono::seconds(30);
    }
    if (!remote.empty()) return;
    if (now >= recovery_deadline) return;
    for (auto &slot : slots) {
      if (slot.agent && slot.agent->state() != IceState::failed) continue;
      if (slot.attempts >= 3 || now < slot.retry_at) continue;
      if (selected == slot.agent.get()) selected = nullptr;
      slot.agent.reset();
      Build(slot);
    }
  }
};

IceTransport::IceTransport() : impl_(std::make_unique<Impl>()) {}
IceTransport::~IceTransport() { Stop(); }
bool IceTransport::Start(const IceConfig &config) {
  Stop();
  std::lock_guard lock(impl_->mutex);
  if (config.tcp_direct) {
    impl_->tcp = std::make_unique<TcpDirectAgent>();
    impl_->running = impl_->tcp->Start(config);
    return impl_->running;
  }
  if (!config.manage_network_paths) {
    impl_->single = std::make_unique<IceAgent>();
    impl_->running = impl_->single->Start(config);
    return impl_->running;
  }
  if (config.network_path.selected() || config.network_paths.empty() || config.network_paths.size() > 4 ||
      !config.bind_address.empty() || config.port_range_begin || config.port_range_end ||
      config.mapping_second_port || config.filtering_second_port || !config.pcp_gateway.empty() ||
      config.allow_relay || config.relay_only || !config.turn_servers.empty() ||
      (!config.stun_host.empty() && !Endpoint::Parse(config.stun_host, config.stun_port))) return false;
  impl_->config = config;
  impl_->ufrag = RandomCredential(8); impl_->password = RandomCredential(32);
  if (impl_->ufrag.empty() || impl_->password.empty()) return false;
  impl_->running = true;
  impl_->Refresh(config.network_paths);
  return std::any_of(impl_->slots.begin(), impl_->slots.end(), [](const auto &s) { return s.agent != nullptr; });
}
void IceTransport::Stop() {
  std::lock_guard lock(impl_->mutex);
  impl_->selected = nullptr;
  impl_->tcp.reset(); impl_->single.reset(); impl_->slots.clear(); impl_->inventory.clear();
  OPENSSL_cleanse(impl_->password.data(), impl_->password.size());
  OPENSSL_cleanse(impl_->remote.data(), impl_->remote.size());
  impl_->password.clear(); impl_->ufrag.clear(); impl_->remote.clear();
  impl_->config = {};
  impl_->running = impl_->gathering = impl_->changed = impl_->ever_connected = impl_->recovering = false;
  impl_->revision = impl_->attempts = impl_->switches = 0;
  impl_->emitted = IceState::disconnected; impl_->emitted_path.clear();
}
bool IceTransport::BeginGather() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->BeginGather();
  if (!impl_->running) return false;
  if (impl_->single) return impl_->single->BeginGather();
  if (impl_->gathering) return true;
  impl_->gathering = true; impl_->began = Clock::now();
  bool any = false;
  for (auto &slot : impl_->slots) {
    if (slot.agent && slot.agent->BeginGather()) any = true;
    else slot.agent.reset();
  }
  return any;
}
bool IceTransport::Gather(std::chrono::milliseconds timeout) {
  if (!BeginGather()) return false;
  const auto deadline = Clock::now() + timeout;
  do {
    if (ReadyForSignaling()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (Clock::now() < deadline);
  return false;
}
bool IceTransport::Gathered() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->Gathered();
  if (impl_->single) return impl_->single->Gathered();
  bool any = false;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    any = true; if (!slot.agent->Gathered()) return false;
  }
  return any;
}
bool IceTransport::ReadyForSignaling() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->Gathered();
  if (impl_->single) return impl_->single->ReadyForSignaling();
  bool any = false, all = true;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    const bool ready = slot.agent->ReadyForSignaling(); any |= ready; all &= ready;
  }
  return any && (all || Clock::now() - impl_->began >= std::chrono::milliseconds(1500));
}
IceState IceTransport::state() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->state();
  if (impl_->single) return impl_->single->state();
  if (impl_->Choose()) return IceState::connected;
  if (!impl_->running) return IceState::disconnected;
  if (impl_->remote.empty()) return IceState::gathering;
  if (Clock::now() >= impl_->recovery_deadline) return IceState::failed;
  return IceState::connecting;
}
std::string IceTransport::LocalDescription() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->LocalDescription();
  if (impl_->single) return impl_->single->LocalDescription();
  std::string result = "a=ice-ufrag:" + impl_->ufrag + "\r\na=ice-pwd:" + impl_->password + "\r\na=x-sovkit-recovery:1\r\n";
  std::set<std::string> candidates;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    std::istringstream lines(slot.agent->LocalDescription());
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.starts_with("a=candidate:")) candidates.insert(line);
    }
  }
  if (candidates.empty()) return {};
  for (const auto &line : candidates) result += line + "\r\n";
  result += "a=end-of-candidates\r\n";
  return result.size() < 4096 ? result : std::string{};
}
bool IceTransport::SetRemoteDescription(std::string_view sdp) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->SetRemoteDescription(sdp);
  if (impl_->single) return impl_->single->SetRemoteDescription(sdp);
  if (!impl_->running || sdp.empty() || sdp.size() >= 4096 || sdp.find('\0') != std::string_view::npos ||
      (!impl_->remote.empty() && impl_->remote != sdp)) return false;
  bool any = false;
  for (auto &slot : impl_->slots) if (slot.agent) any |= slot.agent->SetRemoteDescription(sdp);
  if (!any) return false;
  impl_->remote = sdp;
  impl_->recovery_deadline = Clock::now() + std::chrono::seconds(30);
  return true;
}
bool IceTransport::Send(std::string_view datagram) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->Send(datagram);
  auto *agent = impl_->Choose();
  return agent && agent->Send(datagram);
}
IceSelectedPath IceTransport::SelectedPath() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->SelectedPath();
  auto *agent = impl_->Choose(); return agent ? agent->SelectedPath() : IceSelectedPath{};
}
bool IceTransport::SelectedPathAllowed() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->state() == IceState::connected;
  auto *agent = impl_->Choose(); return agent && agent->SelectedPathAllowed();
}
void IceTransport::UpdateNetworkPaths(const std::vector<NetworkPath> &paths) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return; // TCP v1 has no in-place migration.
  if (impl_->running && !impl_->single) impl_->Refresh(paths);
}
std::vector<IceEvent> IceTransport::TakeEvents() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->TakeEvents();
  if (impl_->single) return impl_->single->TakeEvents();
  impl_->Tick();
  std::vector<IceEvent> events;
  // Drain every verified socket: peers may temporarily choose different
  // working pairs. Noise/QUIC continues authenticating all application data.
  for (auto &slot : impl_->slots) if (slot.agent) {
    for (auto &event : slot.agent->TakeEvents())
      if (event.type == IceEventType::datagram && Connected(*slot.agent)) {
        slot.received_at = Clock::now();
        events.push_back(std::move(event));
      }
  }
  const auto current = state();
  const auto path = SelectedPath();
  const auto key = path.local_address + "/" + path.remote_address;
  if (current == IceState::connected) impl_->ever_connected = true;
  if (impl_->changed || current != impl_->emitted || key != impl_->emitted_path) {
    events.insert(events.begin(), {IceEventType::state_changed, current, "network-paths", {}});
    impl_->changed = false; impl_->emitted = current; impl_->emitted_path = key;
  }
  return events;
}
IceRecoveryStats IceTransport::RecoveryStatistics() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return {};
  IceRecoveryStats result;
  if (!impl_->running || impl_->single) return result;
  result.enabled = true; result.available_paths = impl_->inventory.size();
  result.revision = impl_->revision; result.attempts = impl_->attempts; result.switches = impl_->switches;
  for (const auto &slot : impl_->slots) if (slot.agent) ++result.live_paths;
  result.state = impl_->Choose() ? "connected" : impl_->inventory.empty() ? "offline" :
      impl_->remote.empty() ? (ReadyForSignaling() ? "waiting-peer" : "trying") :
      (!impl_->remote.empty() && impl_->slots.empty()) ? "needs-invitation" :
      Clock::now() >= impl_->recovery_deadline ? "needs-invitation" :
      impl_->ever_connected ? "rebuilding" : "trying";
  return result;
}
bool IceTransport::PeerSupportsRecovery() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return false;
  return !impl_->single && impl_->remote.find("\na=x-sovkit-recovery:1\r\n") != std::string::npos;
}

IceQueueStats IceTransport::QueueStatistics() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->QueueStatistics();
  if (impl_->single) return impl_->single->QueueStatistics();
  IceQueueStats result;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    const auto s = slot.agent->QueueStatistics();
    result.dropped_datagrams += s.dropped_datagrams; result.peak_events += s.peak_events;
  }
  return result;
}
IceGatherStats IceTransport::GatherStatistics() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->GatherStatistics();
  if (impl_->single) return impl_->single->GatherStatistics();
  IceGatherStats result;
  std::set<std::string> remote_families;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    const auto s = slot.agent->GatherStatistics(); AddCounts(result.local, s.local);
    const auto family = slot.agent->NetworkPathStatistics().family;
    if (remote_families.insert(family).second) AddCounts(result.remote, s.remote);
  }
  result.complete = Gathered(); result.signaling_ready = ReadyForSignaling();
  result.remote_description_set = !impl_->remote.empty();
  return result;
}
IceCaptureEndpoints IceTransport::CaptureEndpoints() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->CaptureEndpoints();
  if (impl_->single) return impl_->single->CaptureEndpoints();
  IceCaptureEndpoints result;
  std::set<std::string> remote;
  for (const auto &slot : impl_->slots) if (slot.agent) {
    const auto endpoints = slot.agent->CaptureEndpoints();
    result.local.insert(result.local.end(), endpoints.local.begin(), endpoints.local.end());
    for (const auto &endpoint : endpoints.remote)
      if (remote.insert(endpoint.address + "/" + std::to_string(endpoint.port) + "/" + endpoint.type).second)
        result.remote.push_back(endpoint);
  }
  return result;
}
// These diagnostics describe the selected socket (first available before
// selection). Attempt counts and inventory state are separately aggregated.
#define SOVKIT_ICE_INSPECT(Type, Method) \
Type IceTransport::Method() const { \
  std::lock_guard lock(impl_->mutex); \
  auto *agent = impl_->Inspect(); return agent ? agent->Method() : Type{}; \
}
SOVKIT_ICE_INSPECT(IceCheckStats, CheckStatistics)
SOVKIT_ICE_INSPECT(IceEndpointStats, EndpointStatistics)
SOVKIT_ICE_INSPECT(IceMappingStats, MappingStatistics)
SOVKIT_ICE_INSPECT(IceMappingStats, FilteringStatistics)
SOVKIT_ICE_INSPECT(IceGatewayMappingStats, GatewayMappingStatistics)
NetworkPathStatus IceTransport::NetworkPathStatistics() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->tcp) return impl_->tcp->NetworkPathStatistics();
  auto *agent = impl_->Inspect(); return agent ? agent->NetworkPathStatistics() : NetworkPathStatus{};
}
TcpDirectStats IceTransport::TcpStatistics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->tcp ? impl_->tcp->Statistics() : TcpDirectStats{};
}
#undef SOVKIT_ICE_INSPECT
} // namespace libnet
