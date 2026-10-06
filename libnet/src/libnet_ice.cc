#include "libnet_ice.h"
#include "libnet_uv.h"

#include <juice/juice.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <utility>

namespace libnet {
namespace {

constexpr std::size_t kMaximumQueuedEvents = 256;
constexpr std::size_t kMaximumReceivedDatagramBytes = 64 * 1024;
constexpr auto kMappedCandidateGrace = std::chrono::milliseconds(250);

// Use the separate numeric address in SDP, never an ambiguous IPv6:port string.
std::string CandidateFamily(std::string_view candidate) {
  std::istringstream fields{std::string(candidate)};
  std::string address;
  for (int i = 0; i <= 4; ++i) if (!(fields >> address)) return "unknown";
  const auto endpoint = Endpoint::Parse(address, 1);
  if (!endpoint) return "unknown";
  if (endpoint->family() == AddressFamily::ipv4) return "ipv4";
  const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(endpoint->sockaddr_ptr());
  const auto *bytes = v6->sin6_addr.s6_addr;
  // IPv4-mapped IPv6 is still an IPv4 path, not IPv6 acceptance evidence.
  if (std::all_of(bytes, bytes + 10, [](auto b) { return b == 0; }) &&
      bytes[10] == 0xff && bytes[11] == 0xff) return "ipv4";
  return "ipv6";
}

bool FamilyAllowed(std::string_view family, IceAddressPolicy policy) {
  return policy == IceAddressPolicy::automatic ||
         (policy == IceAddressPolicy::ipv4 && family == "ipv4") ||
         (policy == IceAddressPolicy::ipv6 && family == "ipv6");
}

std::string FilterFamilyCandidates(std::string_view sdp, IceAddressPolicy policy) {
  if (policy == IceAddressPolicy::automatic) return std::string(sdp);
  std::string output;
  std::size_t begin = 0;
  while (begin < sdp.size()) {
    const auto newline = sdp.find('\n', begin);
    const auto end = newline == std::string_view::npos ? sdp.size() : newline + 1;
    const auto line = sdp.substr(begin, end - begin);
    const auto start = line.find_first_not_of(" \t\r");
    const auto trimmed = start == std::string_view::npos ? std::string_view{} : line.substr(start);
    if (!trimmed.starts_with("a=candidate:") || FamilyAllowed(CandidateFamily(trimmed), policy))
      output.append(line);
    begin = end;
  }
  return output;
}

void CountCandidate(std::string_view candidate, IceCandidateCounts &counts) {
  std::istringstream fields{std::string(candidate)};
  std::string token, address, type;
  for (int index = 0; index < 8; ++index) {
    if (!(fields >> token)) return;
    if (index == 4) address = token;
    if (index == 6 && token != "typ") return;
    if (index == 7) type = token;
  }
  if (type == "host") ++counts.host;
  else if (type == "srflx") ++counts.srflx;
  else if (type == "prflx") ++counts.prflx;
  else if (type == "relay") ++counts.relay;
  else return;
  auto endpoint = Endpoint::Parse(address, 1);
  if (!endpoint) { ++counts.other; return; }
  if (endpoint->family() == AddressFamily::ipv6 && CandidateFamily(candidate) == "ipv4") {
    const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(endpoint->sockaddr_ptr());
    sockaddr_in mapped{};
    mapped.sin_family = AF_INET;
    std::memcpy(&mapped.sin_addr, v6->sin6_addr.s6_addr + 12, 4);
    endpoint = Endpoint::FromSockaddr(reinterpret_cast<const sockaddr *>(&mapped), sizeof(mapped));
  }
  if (!endpoint) { ++counts.other; return; }
  if (endpoint->family() == AddressFamily::ipv6) ++counts.ipv6;
  else ++counts.ipv4;
  if (endpoint->IsGlobal()) {
    if (endpoint->family() == AddressFamily::ipv6) ++counts.global_ipv6;
    else ++counts.global_ipv4;
  } else if (endpoint->family() == AddressFamily::ipv6) {
    const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(endpoint->sockaddr_ptr());
    const auto *bytes = v6->sin6_addr.s6_addr;
    if (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ++counts.link_local;
    else if ((bytes[0] & 0xfe) == 0xfc) ++counts.unique_local_ipv6;
    else ++counts.other;
  } else {
    const auto *v4 = reinterpret_cast<const sockaddr_in *>(endpoint->sockaddr_ptr());
    const auto ip = ntohl(v4->sin_addr.s_addr);
    if ((ip >> 16) == 0xa9fe) ++counts.link_local;
    else if ((ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8)
      ++counts.private_ipv4;
    else ++counts.other;
  }
}

std::vector<IceCandidateEndpoint> CaptureCandidates(std::string_view sdp) {
  std::vector<IceCandidateEndpoint> result;
  std::istringstream lines{std::string(sdp)};
  std::string line;
  while (result.size() < 64 && std::getline(lines, line)) {
    if (!line.starts_with("a=candidate:")) continue;
    std::istringstream fields(line);
    std::string foundation, component, protocol, priority, address, port, typ, type;
    if (!(fields >> foundation >> component >> protocol >> priority >> address >> port >> typ >> type) ||
        component != "1" || protocol != "UDP" || typ != "typ" ||
        (type != "host" && type != "srflx" && type != "prflx") || port.empty() || port.size() > 5 ||
        port.find_first_not_of("0123456789") != std::string::npos) continue;
    const auto number = std::stoul(port);
    if (number == 0 || number > 65535) continue;
    const auto endpoint = Endpoint::Parse(address, static_cast<std::uint16_t>(number));
    if (!endpoint || endpoint->family() != AddressFamily::ipv4) continue;
    result.push_back({endpoint->address(), type, endpoint->port()});
  }
  return result;
}

IceState ConvertState(juice_state_t value) {
  switch (value) {
  case JUICE_STATE_GATHERING:
    return IceState::gathering;
  case JUICE_STATE_CONNECTING:
    return IceState::connecting;
  case JUICE_STATE_CONNECTED:
    return IceState::connected;
  case JUICE_STATE_COMPLETED:
    return IceState::completed;
  case JUICE_STATE_FAILED:
    return IceState::failed;
  case JUICE_STATE_DISCONNECTED:
  default:
    return IceState::disconnected;
  }
}

std::string CandidateType(std::string_view candidate) {
  std::istringstream fields{std::string(candidate)};
  std::string token;
  for (int index = 0; index < 7; ++index)
    if (!(fields >> token))
      return "unknown";
  if (token != "typ" || !(fields >> token))
    return "unknown";
  return token;
}

bool DirectCandidateType(std::string_view type) {
  return type == "host" || type == "srflx" || type == "prflx";
}

bool DirectDescription(std::string_view sdp) {
  if (sdp.find('\0') != std::string_view::npos)
    return false;
  std::istringstream lines{std::string(sdp)};
  std::string line;
  while (std::getline(lines, line)) {
    const auto start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos)
      continue;
    const std::string_view candidate(line.data() + start, line.size() - start);
    if (candidate.starts_with("a=candidate:") &&
        !DirectCandidateType(CandidateType(candidate)))
      return false;
  }
  return true;
}

bool ContainsRelay(std::string_view candidate) {
  return CandidateType(candidate) == "relay";
}

/// Preserve SDP session attributes and credentials while limiting candidate
/// disclosure for deterministic TURN-only diagnostics.
std::string FilterRelayCandidates(std::string_view sdp) {
  std::string output;
  std::size_t begin = 0;
  while (begin < sdp.size()) {
    const std::size_t newline = sdp.find('\n', begin);
    const std::size_t end =
        newline == std::string_view::npos ? sdp.size() : newline + 1;
    const std::string_view line = sdp.substr(begin, end - begin);
    if (!line.starts_with("a=candidate:") || ContainsRelay(line))
      output.append(line);
    begin = end;
  }
  return output;
}

} // namespace

struct IceAgent::Impl {
  mutable std::mutex mutex;
  std::condition_variable gathered_cv;
  juice_agent_t *agent = nullptr;
  IceState state = IceState::disconnected;
  bool gathered = false;
  bool relay_only = false;
  bool allow_relay = false;
  IceAddressPolicy address_policy = IceAddressPolicy::automatic;
  NetworkPath network_path;
  std::atomic<int> network_path_state{0};
  static int PrepareSocket(std::uintptr_t socket, int family, void *user) {
    auto &self = *static_cast<Impl *>(user);
    bool success = false;
    try { success = PrepareNetworkPathSocket(self.network_path, socket, family); }
    catch (...) { /* No exception may cross libjuice's C callback. */ }
    self.network_path_state.store(success ? 1 : 2);
    return success ? 0 : -1;
  }
  std::vector<IceEvent> events;
  std::function<void()> event_ready;
  IceQueueStats queue_stats;
  IceCandidateCounts local_counts, remote_counts;
  std::vector<IceCandidateEndpoint> remote_capture_endpoints;
  bool remote_description_set = false;
  std::chrono::steady_clock::time_point first_mapping{};
  std::chrono::steady_clock::time_point started_at{}, remote_description_at{};

  bool ReadyLocked() const {
    if (address_policy != IceAddressPolicy::automatic &&
        local_counts.host + local_counts.srflx + local_counts.prflx + local_counts.relay == 0)
      return false;
    return gathered || (!allow_relay && local_counts.srflx > 0 &&
        std::chrono::steady_clock::now() >= first_mapping + kMappedCandidateGrace);
  }

  void EnqueueLocked(IceEvent event) {
    if (events.size() >= kMaximumQueuedEvents) {
      const auto datagram = std::find_if(
          events.begin(), events.end(), [](const IceEvent &queued) {
            return queued.type == IceEventType::datagram;
          });
      // Never evict control state for a new datagram. UDP overload is loss,
      // not backpressure; count it instead of claiming reliable buffering.
      if (datagram == events.end() && event.type == IceEventType::datagram) {
        ++queue_stats.dropped_datagrams;
        return;
      }
      if (datagram != events.end()) ++queue_stats.dropped_datagrams;
      events.erase(datagram == events.end() ? events.begin() : datagram);
    }
    events.push_back(std::move(event));
    queue_stats.peak_events = (std::max)(queue_stats.peak_events, events.size());
    if (event_ready) event_ready();
  }

  static void OnState(juice_agent_t *, juice_state_t value, void *user) {
    auto &self = *static_cast<Impl *>(user);
    const IceState converted = ConvertState(value);
    std::lock_guard<std::mutex> lock(self.mutex);
    self.state = converted;
    self.EnqueueLocked({IceEventType::state_changed,
                        converted,
                        juice_state_to_string(value),
                        {}});
  }

  static void OnCandidate(juice_agent_t *, const char *sdp, void *user) {
    if (sdp == nullptr)
      return;
    auto &self = *static_cast<Impl *>(user);
    std::lock_guard<std::mutex> lock(self.mutex);
    if (!FamilyAllowed(CandidateFamily(sdp), self.address_policy)) return;
    CountCandidate(sdp, self.local_counts);
    if (self.local_counts.srflx > 0 && self.first_mapping == std::chrono::steady_clock::time_point{})
      self.first_mapping = std::chrono::steady_clock::now();
    self.gathered_cv.notify_all();
    if (!self.relay_only || ContainsRelay(sdp))
      self.EnqueueLocked(
          {IceEventType::candidate, self.state, std::string(sdp), {}});
  }

  static void OnGathered(juice_agent_t *, void *user) {
    auto &self = *static_cast<Impl *>(user);
    {
      std::lock_guard<std::mutex> lock(self.mutex);
      self.gathered = true;
      self.EnqueueLocked({IceEventType::gathering_done, self.state, {}, {}});
    }
    self.gathered_cv.notify_all();
  }

  static void OnReceive(juice_agent_t *, const char *data, std::size_t size,
                        void *user) {
    if (data == nullptr || size == 0 || size > kMaximumReceivedDatagramBytes)
      return;
    auto &self = *static_cast<Impl *>(user);
    IceEvent event;
    event.type = IceEventType::datagram;
    {
      std::lock_guard<std::mutex> lock(self.mutex);
      event.state = self.state;
      event.datagram.assign(reinterpret_cast<const std::uint8_t *>(data),
                            reinterpret_cast<const std::uint8_t *>(data) +
                                size);
      self.EnqueueLocked(std::move(event));
    }
  }
};

IceAgent::IceAgent() : impl_(std::make_unique<Impl>()) {}
IceAgent::~IceAgent() { Stop(); }

bool IsOnLinkGateway(std::string_view local_address, std::string_view gateway) {
  const auto local = Endpoint::Parse(local_address, 0);
  const auto remote = Endpoint::Parse(gateway, 5351);
  if (!local || !remote || local->family() != AddressFamily::ipv4 ||
      remote->family() != AddressFamily::ipv4) return false;
  const auto local_ip = reinterpret_cast<const sockaddr_in *>(local->sockaddr_ptr())->sin_addr.s_addr;
  const auto remote_ip = reinterpret_cast<const sockaddr_in *>(remote->sockaddr_ptr())->sin_addr.s_addr;
  const auto gateway_host = ntohl(remote_ip), local_host = ntohl(local_ip);
  if (!local_host || !gateway_host || gateway_host >= 0xe0000000U ||
      local_host >= 0xe0000000U || (local_ip == remote_ip && (local_host >> 24) != 127)) return false;
  uv_interface_address_t *interfaces = nullptr;
  int count = 0;
  if (uv_interface_addresses(&interfaces, &count) != 0) return false;
  bool found = false;
  for (int i = 0; i < count; ++i) {
    const auto &v = interfaces[i];
    if (v.address.address4.sin_family != AF_INET || v.address.address4.sin_addr.s_addr != local_ip) continue;
    const auto mask = v.netmask.netmask4.sin_addr.s_addr;
    if (mask && (local_ip & mask) == (remote_ip & mask) &&
        (remote_ip & ~mask) != 0 && (remote_ip & ~mask) != ~mask) found = true;
  }
  uv_free_interface_addresses(interfaces, count);
  return found;
}

bool IceAgent::Start(const IceConfig &config) {
  if (config.tcp_direct) return false; // Never silently reinterpret TCP as UDP.
  Stop();
  if (config.mapping_second_port && config.filtering_second_port) return false;
  if (config.filtering_second_port && (config.port_range_begin || config.port_range_end)) return false;
  const auto diagnostic_port = config.mapping_second_port ? config.mapping_second_port : config.filtering_second_port;
  if (diagnostic_port) {
    const auto endpoint = Endpoint::Parse(config.stun_host, config.stun_port);
    if (config.address_policy != IceAddressPolicy::ipv4 || config.allow_relay ||
        !endpoint || endpoint->family() != AddressFamily::ipv4 ||
        diagnostic_port < 1024 || config.stun_port < 1024 ||
        diagnostic_port == config.stun_port) return false;
    const auto ip = ntohl(reinterpret_cast<const sockaddr_in *>(endpoint->sockaddr_ptr())->sin_addr.s_addr);
    if (!ip || ip >= 0xe0000000U) return false;
  }
  if (config.address_policy != IceAddressPolicy::automatic &&
      config.address_policy != IceAddressPolicy::ipv4 &&
      config.address_policy != IceAddressPolicy::ipv6) return false;
  std::string bind_address = config.bind_address;
  if (config.network_path.selected()) {
    if (!ValidateNetworkPath(config.network_path) ||
        (!bind_address.empty() && bind_address != config.network_path.local_address)) return false;
    bind_address = config.network_path.local_address;
    if (!config.stun_host.empty()) {
      const auto source = Endpoint::Parse(bind_address, 0);
      const auto server = Endpoint::Parse(config.stun_host, config.stun_port);
      if (!source || !server || source->family() != server->family()) return false;
    }
  }
  if (config.address_policy != IceAddressPolicy::automatic) {
    if (bind_address.empty()) {
      bind_address = config.address_policy == IceAddressPolicy::ipv4 ? "0.0.0.0" : "::";
    } else {
      const auto endpoint = Endpoint::Parse(bind_address, 0);
      if (!endpoint || (endpoint->family() == AddressFamily::ipv4) !=
          (config.address_policy == IceAddressPolicy::ipv4)) return false;
    }
  }
  if ((!config.allow_relay &&
       (config.relay_only || !config.turn_servers.empty())) ||
      (!config.stun_host.empty() && config.stun_port == 0) ||
      config.turn_servers.size() > 8 ||
      (config.port_range_begin != 0 && config.port_range_end != 0 &&
       config.port_range_begin > config.port_range_end))
    return false;

  std::vector<juice_turn_server_t> turns;
  turns.reserve(config.turn_servers.size());
  for (const TurnServer &server : config.turn_servers) {
    if (server.host.empty() || server.port == 0 || server.username.empty() ||
        server.password.empty())
      return false;
    turns.push_back({server.host.c_str(), server.username.c_str(),
                     server.password.c_str(), server.port});
  }
  juice_config_t value{};
  if (!config.pcp_gateway.empty()) {
    if (config.address_policy != IceAddressPolicy::ipv4 || config.allow_relay ||
        !config.turn_servers.empty() || config.mapping_second_port || config.filtering_second_port ||
        !IsOnLinkGateway(config.bind_address, config.pcp_gateway)) return false;
    const auto gateway = Endpoint::Parse(config.pcp_gateway, 5351);
    const auto local = Endpoint::Parse(config.bind_address, 0);
    value.sovkit_pcp_gateway_ipv4 = reinterpret_cast<const sockaddr_in *>(gateway->sockaddr_ptr())->sin_addr.s_addr;
    value.sovkit_pcp_local_ipv4 = reinterpret_cast<const sockaddr_in *>(local->sockaddr_ptr())->sin_addr.s_addr;
    if (RAND_bytes(value.sovkit_pcp_nonce, sizeof(value.sovkit_pcp_nonce)) != 1) return false;
  }
  value.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
  value.sovkit_endpoint_diagnostics = config.endpoint_diagnostics;
  value.sovkit_mapping_second_port = config.mapping_second_port;
  value.sovkit_filtering_second_port = config.filtering_second_port;
  value.stun_server_host =
      config.stun_host.empty() ? nullptr : config.stun_host.c_str();
  value.stun_server_port = config.stun_port;
  value.turn_servers = turns.empty() ? nullptr : turns.data();
  value.turn_servers_count = static_cast<int>(turns.size());
  value.bind_address =
      bind_address.empty() ? nullptr : bind_address.c_str();
  value.local_port_range_begin = config.port_range_begin;
  value.local_port_range_end = config.port_range_end;
  value.cb_state_changed = Impl::OnState;
  value.cb_candidate = Impl::OnCandidate;
  value.cb_gathering_done = Impl::OnGathered;
  value.cb_recv = Impl::OnReceive;
  value.user_ptr = impl_.get();
  value.sovkit_prepare_socket = config.network_path.selected() ? Impl::PrepareSocket : nullptr;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->gathered = false;
    impl_->relay_only = config.relay_only;
    impl_->allow_relay = config.allow_relay;
    impl_->address_policy = config.address_policy;
    impl_->network_path = config.network_path;
    impl_->network_path_state.store(0);
    impl_->event_ready = config.event_ready;
    impl_->queue_stats = {};
    impl_->local_counts = {};
    impl_->remote_counts = {};
    impl_->remote_capture_endpoints.clear();
    impl_->remote_description_set = false;
    impl_->started_at = std::chrono::steady_clock::now();
    impl_->remote_description_at = {};
    impl_->first_mapping = {};
    impl_->state = IceState::disconnected;
    impl_->events.clear();
  }
  impl_->agent = juice_create(&value);
  OPENSSL_cleanse(value.sovkit_pcp_nonce, sizeof(value.sovkit_pcp_nonce));
  return impl_->agent != nullptr;
}

void IceAgent::Stop() {
  juice_agent_t *agent = std::exchange(impl_->agent, nullptr);
  if (agent != nullptr)
    juice_destroy(agent);
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->state = IceState::disconnected;
  impl_->gathered = false;
  impl_->local_counts = {};
  impl_->remote_counts = {};
  impl_->remote_capture_endpoints.clear();
  impl_->remote_description_set = false;
  impl_->first_mapping = {};
  impl_->event_ready = {};
  impl_->network_path = {};
  impl_->network_path_state.store(0);
  impl_->events.clear();
}

bool IceAgent::SetLocalCredentials(std::string_view ufrag, std::string_view password) {
  if (!impl_->agent || ufrag.empty() || ufrag.size() > 32 || password.size() < 22 ||
      password.size() > 256 || ufrag.find('\0') != std::string_view::npos ||
      password.find('\0') != std::string_view::npos) return false;
  return juice_set_local_ice_attributes(impl_->agent, std::string(ufrag).c_str(),
                                       std::string(password).c_str()) == JUICE_ERR_SUCCESS;
}

NetworkPathStatus IceAgent::NetworkPathStatistics() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  NetworkPathStatus status;
  status.selected = impl_->network_path.selected();
  if (status.selected) {
    const auto address = Endpoint::Parse(impl_->network_path.local_address, 0);
    status.family = address && address->family() == AddressFamily::ipv4 ? "ipv4" : "ipv6";
    status.binding_method = NetworkPathBindingMethod();
    status.state = impl_->network_path_state.load() == 1 ? "prepared" :
                   impl_->network_path_state.load() == 2 ? "failed" : "not-started";
  }
  return status;
}

IceQueueStats IceAgent::QueueStatistics() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->queue_stats;
}

static IceMappingStats DiagnosticStatistics(const juice_sovkit_mapping_stats_t &stats) {
  IceMappingStats result;
  result.enabled = stats.enabled != 0;
  constexpr const char *states[] = {"not-started", "running", "complete", "timer-gap", "invalid"};
  result.state = stats.state >= 0 && stats.state < 5 ? states[stats.state] : "invalid";
  result.age_ms = stats.age_ms;
  result.rejected_responses = stats.rejected_responses;
  result.duplicate_responses = stats.duplicate_responses;
  if (!result.enabled || stats.state == 0) return result;
  constexpr const char *sample_states[] = {"pending", "waiting", "response", "timeout", "send-error"};
  for (const auto &entry : stats.samples) {
    IceMappingSample sample;
    std::copy(std::begin(entry.transaction_id), std::end(entry.transaction_id), sample.transaction.begin());
    sample.state = entry.state < 5 ? sample_states[entry.state] : "invalid";
    sample.server_port = entry.server_port;
    sample.mapped_port = entry.mapped_port;
    sample.sends = entry.sends;
    sample.send_status = entry.send_status;
    sample.sent_ms = entry.sent_ms;
    sample.received_ms = entry.received_ms;
    if (entry.state == 2) {
      char address[INET_ADDRSTRLEN]{};
      if (uv_inet_ntop(AF_INET, &entry.mapped_ipv4, address, sizeof(address)) == 0)
        sample.mapped_address = address;
    }
    result.samples.push_back(std::move(sample));
  }
  return result;
}

IceMappingStats IceAgent::MappingStatistics() const {
  juice_sovkit_mapping_stats_t stats{};
  if (!impl_->agent || juice_sovkit_get_mapping_stats(impl_->agent, &stats) != JUICE_ERR_SUCCESS) return {};
  return DiagnosticStatistics(stats);
}

IceMappingStats IceAgent::FilteringStatistics() const {
  juice_sovkit_mapping_stats_t stats{};
  if (!impl_->agent || juice_sovkit_get_filtering_stats(impl_->agent, &stats) != JUICE_ERR_SUCCESS) return {};
  return DiagnosticStatistics(stats);
}

IceGatewayMappingStats IceAgent::GatewayMappingStatistics() const {
  juice_sovkit_pcp_stats_t s{};
  if (!impl_->agent || juice_sovkit_get_pcp_stats(impl_->agent, &s) != JUICE_ERR_SUCCESS) return {};
  static constexpr const char *states[] = {"not-started", "requesting", "mapped", "unsupported",
      "rejected", "timeout", "expired", "mapping-changed", "error"};
  return {s.enabled != 0, s.state >= 0 && s.state < 9 ? states[s.state] : "error",
      s.result_code, s.send_status, s.internal_port, s.external_port, s.lifetime_seconds,
      s.remaining_ms, s.requests_sent, s.rejected_responses, s.renewals};
}

bool IceAgent::started() const { return impl_->agent != nullptr; }

IceState IceAgent::state() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->state;
}

bool IceAgent::Gather(std::chrono::milliseconds timeout) {
  if (timeout <= std::chrono::milliseconds::zero() || !BeginGather())
    return false;
  std::unique_lock<std::mutex> lock(impl_->mutex);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    // Never acquire libjuice's conn_lock while holding the callback mutex.
    lock.unlock();
    const bool ready = ReadyForSignaling();
    lock.lock();
    if (ready) return true;
    if (std::chrono::steady_clock::now() >= deadline) return false;
    auto wake = (std::min)(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
    if (!impl_->allow_relay && impl_->local_counts.srflx > 0 &&
        impl_->first_mapping + kMappedCandidateGrace > std::chrono::steady_clock::now())
      wake = (std::min)(wake, impl_->first_mapping + kMappedCandidateGrace);
    impl_->gathered_cv.wait_until(lock, wake);
  }
}

bool IceAgent::BeginGather() {
  if (impl_->agent == nullptr)
    return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->gathered = false;
  }
  return juice_gather_candidates(impl_->agent) == JUICE_ERR_SUCCESS;
}

bool IceAgent::Gathered() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->gathered;
}

bool IceAgent::ReadyForSignaling() const {
  const auto gateway = GatewayMappingStatistics();
  if (gateway.enabled && (gateway.state == "not-started" || gateway.state == "requesting")) return false;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->ReadyLocked();
}

IceGatherStats IceAgent::GatherStatistics() const {
  const auto gateway = GatewayMappingStatistics();
  const bool pending = gateway.enabled && (gateway.state == "not-started" || gateway.state == "requesting");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return {impl_->local_counts, impl_->remote_counts, impl_->gathered,
          !pending && impl_->ReadyLocked(), impl_->remote_description_set};
}

IceCheckStats IceAgent::CheckStatistics() const {
  juice_sovkit_check_stats_t value{};
  if (!impl_->agent || juice_sovkit_get_check_stats(impl_->agent, &value) != JUICE_ERR_SUCCESS)
    return {};
  IceCheckStats result{true, value.requests_sent, value.send_errors, value.requests_received,
          value.responses_received, value.responses_sent, value.validation_failures,
          value.check_timeouts, value.pairs_succeeded, value.role_conflicts,
          value.non_symmetric_responses,
          value.udp_datagrams_received,
          value.stun_datagrams_received,
          value.non_stun_datagrams_received,
          value.early_state_drops,
          value.malformed_stun_drops,
          value.binding_requests_observed,
          value.binding_responses_observed,
          value.unmatched_transaction_drops,
          value.unmatched_address_drops,
          value.missing_integrity_drops};
  // Do not hold our mutex while acquiring libjuice's connection lock above.
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto now = std::chrono::steady_clock::now();
  result.age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->started_at).count();
  if (impl_->remote_description_set)
    result.remote_description_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->remote_description_at).count();
  return result;
}

std::string IceAgent::LocalDescription() const {
  if (impl_->agent == nullptr)
    return {};
  char buffer[JUICE_MAX_SDP_STRING_LEN]{};
  if (juice_get_local_description(impl_->agent, buffer, sizeof(buffer)) !=
      JUICE_ERR_SUCCESS)
    return {};
  std::string result(buffer);
  std::lock_guard<std::mutex> lock(impl_->mutex);
  result = FilterFamilyCandidates(result, impl_->address_policy);
  return impl_->relay_only ? FilterRelayCandidates(result) : result;
}

IceCaptureEndpoints IceAgent::CaptureEndpoints() const {
  IceCaptureEndpoints result;
  result.local = CaptureCandidates(LocalDescription());
  std::lock_guard<std::mutex> lock(impl_->mutex);
  result.remote = impl_->remote_capture_endpoints;
  return result;
}

IceEndpointStats IceAgent::EndpointStatistics() const {
  juice_sovkit_check_stats_t value{};
  IceEndpointStats result;
  if (!impl_->agent || juice_sovkit_get_check_stats(impl_->agent, &value) != JUICE_ERR_SUCCESS)
    return result;
  result.enabled = value.endpoint_enabled != 0;
  result.dropped = value.endpoint_dropped;
  static constexpr const char *kKinds[] = {
      "rx-stun", "tx-check-request", "tx-check-other", "tx-server-binding"};
  for (std::uint32_t i = 0; i < (std::min)(value.endpoint_count, std::uint32_t{JUICE_SOVKIT_ENDPOINT_LIMIT}); ++i) {
    const auto &v = value.endpoints[i];
    result.entries.push_back({v.address, v.family == 4 ? "ipv4" : "ipv6",
        v.kind < 4 ? kKinds[v.kind] : "unknown", v.port, v.send_status,
        v.count, v.bytes, v.first_ms, v.last_ms});
  }
  return result;
}

bool IceAgent::SetRemoteDescription(std::string_view sdp) {
  if (impl_->agent == nullptr || sdp.empty() ||
      sdp.size() >= JUICE_MAX_SDP_STRING_LEN)
    return false;
  std::string value(sdp);
  IceAddressPolicy policy;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // Reject the entire mixed offer too: silently dropping relay candidates
    // would conceal a peer policy mismatch from the application.
    if (!impl_->allow_relay && !DirectDescription(sdp))
      return false;
    policy = impl_->address_policy;
  }
  value = FilterFamilyCandidates(value, policy);
  if (policy != IceAddressPolicy::automatic && value.find("a=candidate:") == std::string::npos)
    return false;
  if (juice_set_remote_description(impl_->agent, value.c_str()) != JUICE_ERR_SUCCESS)
    return false;
  IceCandidateCounts counts;
  std::istringstream lines(value);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.starts_with("a=candidate:")) CountCandidate(line, counts);
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->remote_counts = counts;
  impl_->remote_capture_endpoints = CaptureCandidates(value);
  if (!impl_->remote_description_set) impl_->remote_description_at = std::chrono::steady_clock::now();
  impl_->remote_description_set = true;
  return true;
}

bool IceAgent::Send(std::string_view datagram) {
  return impl_->agent != nullptr && !datagram.empty() && SelectedPathAllowed() &&
         juice_send(impl_->agent, datagram.data(), datagram.size()) ==
             JUICE_ERR_SUCCESS;
}

IceSelectedPath IceAgent::SelectedPath() const {
  IceSelectedPath result;
  if (impl_->agent == nullptr)
    return result;
  char local_candidate[JUICE_MAX_CANDIDATE_SDP_STRING_LEN]{};
  char remote_candidate[JUICE_MAX_CANDIDATE_SDP_STRING_LEN]{};
  char local_address[JUICE_MAX_ADDRESS_STRING_LEN]{};
  char remote_address[JUICE_MAX_ADDRESS_STRING_LEN]{};
  if (juice_get_selected_candidates(
          impl_->agent, local_candidate, sizeof(local_candidate),
          remote_candidate, sizeof(remote_candidate)) == JUICE_ERR_SUCCESS) {
    result.local_candidate = local_candidate;
    result.remote_candidate = remote_candidate;
    result.local_type = CandidateType(result.local_candidate);
    result.remote_type = CandidateType(result.remote_candidate);
    result.local_family = CandidateFamily(result.local_candidate);
    result.remote_family = CandidateFamily(result.remote_candidate);
    result.relayed =
        result.local_type == "relay" || result.remote_type == "relay";
  }
  if (juice_get_selected_addresses(
          impl_->agent, local_address, sizeof(local_address), remote_address,
          sizeof(remote_address)) == JUICE_ERR_SUCCESS) {
    result.local_address = local_address;
    result.remote_address = remote_address;
  }
  return result;
}

bool IceAgent::SelectedPathAllowed() const {
  bool allow_relay = false;
  IceAddressPolicy policy;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    allow_relay = impl_->allow_relay;
    policy = impl_->address_policy;
  }
  const auto path = SelectedPath();
  if (path.local_type.empty() || path.remote_type.empty())
    return false;
  if (!FamilyAllowed(path.local_family, policy) || !FamilyAllowed(path.remote_family, policy))
    return false;
  return allow_relay ||
         (DirectCandidateType(path.local_type) &&
          DirectCandidateType(path.remote_type) && !path.relayed);
}

std::vector<IceEvent> IceAgent::TakeEvents() {
  std::vector<IceEvent> result;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    result.swap(impl_->events);
  }
  // Query libjuice outside our callback mutex. No application payload is
  // exposed until the actual selected path has passed the same send policy.
  if (!SelectedPathAllowed()) {
    std::erase_if(result, [](const IceEvent &event) {
      return event.type == IceEventType::datagram;
    });
    for (auto &event : result) {
      if (event.type == IceEventType::state_changed &&
          (event.state == IceState::connected ||
           event.state == IceState::completed)) {
        event.state = IceState::failed;
        event.detail = "selected-path-not-permitted";
      }
    }
  }
  return result;
}

} // namespace libnet
