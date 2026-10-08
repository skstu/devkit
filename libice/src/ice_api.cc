#include <libice/ice.h>
#include <libnet_ice.h>
#include <libnet_uv.h>
#include <juice/juice.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

struct dkice_context {
  std::thread::id owner = std::this_thread::get_id();
  std::unique_ptr<libnet::IceAgent> agent = std::make_unique<libnet::IceAgent>();
  dkice_wake_fn wake = nullptr;
  void *user = nullptr;
  std::atomic<bool> stopping{false};
  bool dispatching = false, gathering = false;
  ~dkice_context() { stopping = true; agent->Stop(); }
};
namespace {
thread_local bool in_wake = false;
int guard(dkice_context *c) {
  if (in_wake) return DKICE_BUSY;
  if (!c) return DKICE_INVALID;
  if (c->owner != std::this_thread::get_id()) return DKICE_THREAD;
  return DKICE_OK;
}
template<class F> int32_t call(dkice_context *c, F body) noexcept {
  if (const auto error = guard(c); error) return error;
  try { return body(); } catch (...) { return DKICE_INTERNAL; }
}
bool text(const char *p, size_t maximum, std::string &result) {
  result.clear();
  if (!p) return true;
  const auto length = strnlen(p, maximum + 1);
  if (length > maximum) return false;
  result.assign(p, length); return true;
}
bool numeric(const std::string &value) {
  return value.empty() || libnet::Endpoint::Parse(value, 0).has_value();
}
uint32_t total(const libnet::IceCandidateCounts &c) {
  return static_cast<uint32_t>(c.host + c.srflx + c.prflx + c.relay);
}
template<size_t N> void copy(char (&out)[N], const std::string &s) {
  std::memcpy(out, s.data(), std::min(s.size(), N-1));
}
void stop(dkice_context *c) {
  c->stopping = true;
  c->agent->Stop(); // joins worker before returning or freeing its callback state
}
}
extern "C" {
uint32_t DKICE_CALL dkice_abi_version(void) { return DKICE_ABI_VERSION; }
const char * DKICE_CALL dkice_version(void) { return DKICE_VERSION; }
const char * DKICE_CALL dkice_backend_version(void) { return "libjuice 1.7.2 (devkit overlay 13)"; }
int32_t DKICE_CALL dkice_create(const dkice_config *config, dkice_context **out) {
  if (!out) return DKICE_INVALID;
  *out = nullptr;
  if (in_wake) return DKICE_BUSY;
  if (!config || config->struct_size != sizeof(*config) || config->abi_version != 1 ||
      config->reserved || config->reserved2 || config->flags & ~3u ||
      (config->address_family != 0 && config->address_family != 4 && config->address_family != 6) ||
      config->turn_server_count > 8 || (config->turn_server_count && !config->turn_servers) ||
      (!(config->flags & DKICE_ALLOW_RELAY) && ((config->flags & DKICE_RELAY_ONLY) || config->turn_server_count)) ||
      ((config->flags & DKICE_RELAY_ONLY) && !config->turn_server_count) ||
      (!!config->port_begin != !!config->port_end) || config->port_begin > config->port_end)
    return DKICE_INVALID;
  try {
    libnet::IceConfig value;
    value.allow_relay = config->flags & DKICE_ALLOW_RELAY;
    value.relay_only = config->flags & DKICE_RELAY_ONLY;
    value.address_policy = config->address_family == 4 ? libnet::IceAddressPolicy::ipv4 :
                           config->address_family == 6 ? libnet::IceAddressPolicy::ipv6 : libnet::IceAddressPolicy::automatic;
    if (!text(config->bind_address, 64, value.bind_address) || !numeric(value.bind_address) ||
        !text(config->stun_address, 64, value.stun_host) || !numeric(value.stun_host) ||
        (!value.stun_host.empty() && !config->stun_port) ||
        !text(config->interface_name, 255, value.network_path.interface_name)) return DKICE_INVALID;
    if (!value.network_path.interface_name.empty()) {
      if (value.bind_address.empty()) return DKICE_INVALID;
      value.network_path.local_address = value.bind_address;
    }
    value.stun_port = config->stun_port; value.port_range_begin = config->port_begin; value.port_range_end = config->port_end;
    for (uint32_t n=0; n<config->turn_server_count; ++n) {
      const auto &input = config->turn_servers[n];
      libnet::TurnServer server;
      if (input.reserved || !input.port || !text(input.host,64,server.host) || server.host.empty() || !numeric(server.host) ||
          !text(input.username,256,server.username) || server.username.empty() ||
          !text(input.password,256,server.password) || server.password.empty()) return DKICE_INVALID;
      server.port = input.port; value.turn_servers.push_back(std::move(server));
    }
    auto c = std::make_unique<dkice_context>(); c->wake = config->wake; c->user = config->user;
    value.event_ready = [p=c.get()] {
      if (p->stopping || !p->wake) return;
      const bool previous = in_wake; in_wake = true;
      try { p->wake(p->user); } catch (...) { /* Foreign callbacks must not throw. */ }
      in_wake = previous;
    };
    if (!c->agent->Start(value)) { stop(c.get()); return DKICE_IO; }
    *out = c.release(); return DKICE_OK;
  } catch (...) { return DKICE_INTERNAL; }
}
int32_t DKICE_CALL dkice_gather(dkice_context *c) {
  return call(c,[&] {
    if (c->stopping || c->gathering) return DKICE_STATE;
    if (!c->agent->BeginGather()) return DKICE_IO;
    c->gathering = true; return DKICE_OK;
  });
}
int32_t DKICE_CALL dkice_local_description(dkice_context *c, char *buffer, size_t capacity, size_t *required) {
  return call(c,[&] {
    if (!required || (!buffer && capacity)) return DKICE_INVALID;
    *required = 0;
    if (c->stopping) return DKICE_STATE;
    const auto value = c->agent->LocalDescription();
    if (value.empty()) return DKICE_STATE;
    *required = value.size()+1;
    if (!buffer || capacity < *required) return DKICE_BUFFER;
    std::memcpy(buffer,value.c_str(),*required); return DKICE_OK;
  });
}
int32_t DKICE_CALL dkice_remote_description(dkice_context *c, const char *data, size_t size) {
  return call(c,[&] {
    if (!data || !size || size >= JUICE_MAX_SDP_STRING_LEN || std::memchr(data,0,size)) return DKICE_INVALID;
    if (c->stopping) return DKICE_STATE;
    return c->agent->SetRemoteDescription({data,size}) ? DKICE_OK : DKICE_INVALID;
  });
}
int32_t DKICE_CALL dkice_send(dkice_context *c, const uint8_t *data, size_t size) {
  return call(c,[&] {
    if (!data || !size || size > 65507) return DKICE_INVALID;
    if (c->stopping || !c->agent->SelectedPathAllowed()) return DKICE_STATE;
    return c->agent->Send({reinterpret_cast<const char *>(data),size}) ? DKICE_OK : DKICE_IO;
  });
}
int32_t DKICE_CALL dkice_dispatch(dkice_context *c, dkice_event_fn callback, void *user) {
  return call(c,[&] {
    if (!callback) return DKICE_INVALID;
    if (c->dispatching) return DKICE_BUSY;
    if (c->stopping) return DKICE_STATE;
    struct DispatchGuard { bool &value; ~DispatchGuard(){ value=false; } } guard{c->dispatching};
    c->dispatching = true;
    const auto events = c->agent->TakeEvents();
    int32_t count=0;
    for (const auto &value : events) {
      if (c->stopping) break;
      dkice_event e{}; e.struct_size=sizeof(e); e.state=static_cast<uint32_t>(value.state);
      e.type=static_cast<uint32_t>(value.type)+1; e.detail=value.detail.c_str();
      e.data=value.datagram.data(); e.size=value.datagram.size();
      callback(user,&e); ++count;
    }
    return count;
  });
}
int32_t DKICE_CALL dkice_get_status(dkice_context *c, dkice_status *out) {
  return call(c,[&] {
    if (!out || out->struct_size != sizeof(*out)) return DKICE_INVALID;
    if (c->stopping) return DKICE_STATE;
    const auto gathered=c->agent->GatherStatistics(); const auto path=c->agent->SelectedPath();
    const auto queue=c->agent->QueueStatistics();
    *out={}; out->struct_size=sizeof(*out); out->state=static_cast<uint32_t>(c->agent->state());
    out->gathering_complete=gathered.complete; out->signaling_ready=gathered.signaling_ready;
    out->path_allowed=c->agent->SelectedPathAllowed(); out->relayed=path.relayed;
    if ((out->state==DKICE_CONNECTED || out->state==DKICE_COMPLETED) && !out->path_allowed) out->state=DKICE_FAILED;
    out->local_candidates=total(gathered.local); out->remote_candidates=total(gathered.remote);
    out->dropped_datagrams=queue.dropped_datagrams; out->peak_queued_events=queue.peak_events;
    copy(out->local_address,path.local_address); copy(out->remote_address,path.remote_address);
    copy(out->local_type,path.local_type); copy(out->remote_type,path.remote_type);
    return DKICE_OK;
  });
}
int32_t DKICE_CALL dkice_stop(dkice_context *c) { return call(c,[&] { stop(c); return DKICE_OK; }); }
int32_t DKICE_CALL dkice_destroy(dkice_context *c) {
  return call(c,[&] { if (c->dispatching) return DKICE_BUSY; stop(c); delete c; return DKICE_OK; });
}
}
