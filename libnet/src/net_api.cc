#include "libnet/net.h"
#include "libnet_quic.h"
#include "libnet_uv.h"
#include "lan_paths.h"
#include <map>
#include <chrono>
#include <openssl/crypto.h>
#include <ngtcp2/version.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace {
std::optional<libnet::Endpoint> endpoint(const dknet_endpoint *p) {
  if (!p || p->reserved || !std::memchr(p->address, 0, sizeof(p->address))) return {};
  return libnet::Endpoint::Parse(p->address, p->port);
}
dknet_endpoint external(const libnet::Endpoint &p) {
  dknet_endpoint out{};
  const auto address=p.address();
  if (address.size() < sizeof(out.address)) {
    std::memcpy(out.address, address.data(), address.size()); out.port=p.port();
  }
  return out;
}
bool bytes(const uint8_t *p, size_t n) { return (p || !n) && n <= 65556; }
std::string_view view(const uint8_t *p, size_t n) {
  return {p ? reinterpret_cast<const char *>(p) : "", n};
}
bool valid_config(const dknet_config *config) {
  return !(!config || config->struct_size!=sizeof(*config) || config->abi_version!=DKNET_ABI_VERSION ||
        config->reserved || !config->on_event || !config->alpn ||
        !*config->alpn || std::strlen(config->alpn)>255 ||
        !config->maximum_connections || config->maximum_connections>256 ||
        !config->maximum_pending_datagrams || config->maximum_pending_datagrams>65536 ||
        !config->maximum_pending_bytes || config->maximum_pending_bytes>64*1024*1024 ||
        !config->quic_tick_ms || config->quic_tick_ms>1000);
}
template<class F> int32_t protect(F &&f) noexcept {
  try { return f(); } catch (...) { return DKNET_INTERNAL; }
}
}

struct dknet_context {
  dknet_config config{};
  std::thread::id owner=std::this_thread::get_id();
  uv_loop_t loop{};
  uv_async_t async{};
  uv_timer_t quic_timer{};
  std::array<uv_timer_t,8> timers{};
  std::mutex gate;
  uint32_t pending_wakes=0;
  bool loop_initialized=false, async_initialized=false, quic_initialized=false;
  size_t timers_initialized=0;
  bool closing=false, running=false, wake_open=false, ticking=false;
  bool dispatching=false, sending=false, callback_failed=false, quic_events_ready=false;
  unsigned callback_depth=0;
  size_t pending_bytes=0, pending_datagrams=0;
  bool blocked=false;
  unsigned rebind_pending=0;
  int32_t rebind_status=DKNET_OK;
  libnet::UdpSocket socket4, socket6;
  libnet::QuicProvider quic;
  bool lan_paths=false;
  size_t maximum_paths=0;
  std::string explicit_interface;
  std::vector<libnet::detail::LanPath> paths;
  std::map<std::string,std::unique_ptr<libnet::UdpSocket>> outputs;
  uint64_t path_generation=1;
  void retire_outputs() {
    ++path_generation;
    // Preserve cancelled send completions so they return the pending budget.
    for (auto& [key, socket] : outputs) socket->Close();
    outputs.clear();
  }
  std::chrono::steady_clock::time_point paths_checked{};
  int refresh_paths(bool force=false) {
    if(!lan_paths)return DKNET_OK;
    const auto now=std::chrono::steady_clock::now();
    if(!force && now-paths_checked<std::chrono::seconds(1))return DKNET_OK;
    paths_checked=now;
    std::vector<libnet::detail::LanPath> next;
    const int status=libnet::detail::ListLanPaths(explicit_interface.c_str(),maximum_paths,next);
    if(status && status!=DKNET_NOT_FOUND){retire_outputs();paths.clear();return status;}
    const bool same=paths.size()==next.size() && std::equal(paths.begin(),paths.end(),next.begin(),[](const auto& a,const auto& b){
      return !std::memcmp(&a.row,&b.row,sizeof(a.row)) && a.android_network==b.android_network && a.preferred==b.preferred;
    });
    if(!same){retire_outputs();paths=std::move(next);}
    return status;
  }
  libnet::DatagramHandler receive;
  libnet::StatusHandler receive_error;
  libnet::UdpSocket *output_for(const libnet::Endpoint &peer) {
    const auto value=peer.address();
    if(!lan_paths || (explicit_interface.empty() && (value=="127.0.0.1" || value=="::1")))
      return peer.family()==libnet::AddressFamily::ipv6?&socket6:&socket4;
    if(refresh_paths()!=DKNET_OK)return nullptr;
    const auto *path=libnet::detail::SelectLanPath(paths,peer);if(!path)return nullptr;
    const auto key=std::string(path->row.name)+"/"+path->row.address;
    if(const auto found=outputs.find(key);found!=outputs.end())return found->second.get();
    if(outputs.size()>=maximum_paths)return nullptr;
    auto local4=socket4.local_endpoint();if(!local4)return nullptr;
    std::string address=path->row.address;
    if(path->row.family==6 && (path->row.flags&DKNET_INTERFACE_LINK_LOCAL))address+="%"+std::to_string(path->row.index);
    const auto local=libnet::Endpoint::Parse(address,local4->port());if(!local)return nullptr;
    auto output=std::make_unique<libnet::UdpSocket>();
    const auto snapshot=*path;
    const auto generation=path_generation;
    if(!output->BindPrepared(&loop,*local,UV_UDP_REUSEADDR | (path->row.family==6?UV_UDP_IPV6ONLY:0),
          [snapshot](uintptr_t socket){return libnet::detail::PrepareLanSocket(snapshot,socket);}) ||
       !output->StartReceive([this,generation](std::string_view data,const libnet::Endpoint& peer,unsigned flags){
         if(generation==path_generation)receive(data,peer,flags);
       },[this,generation](int code){if(generation==path_generation)receive_error(code);}))return nullptr;
    auto *result=output.get();outputs.emplace(key,std::move(output));return result;
  }
  bool affine() const { return owner == std::this_thread::get_id(); }
  bool live() const { return affine() && !closing && !sending; }
  void emit(uint32_t type, uint32_t slot=0, int32_t status=DKNET_OK,
            const libnet::Endpoint &peer={}, std::string_view data={}) noexcept {
    if (closing) return;
    const dknet_event event{sizeof(dknet_event),type,slot,status,external(peer),
      reinterpret_cast<const uint8_t *>(data.data()),data.size()};
    ++callback_depth;
    try { config.on_event(config.user,&event); }
    catch (...) { callback_failed=true; }
    --callback_depth;
    if (callback_failed && !closing) shutdown();
  }
  void update_tick() {
    if (closing) return;
    if (quic.HasPendingEvents() && !quic_events_ready) {
      quic_events_ready=true;
      uv_async_send(&async);
    }
    const bool needed=quic.ConnectionCount()!=0;
    if (needed==ticking) return;
    ticking=needed;
    if (!needed) uv_timer_stop(&quic_timer);
    else uv_timer_start(&quic_timer,[](uv_timer_t *h){
      auto *c=static_cast<dknet_context *>(h->data);
      try { c->quic.Tick(); c->update_tick(); c->emit(DKNET_QUIC_TICK); }
      catch (...) { c->emit(DKNET_ERROR,0,DKNET_INTERNAL); c->shutdown(); }
    },config.quic_tick_ms,config.quic_tick_ms);
  }
  int32_t send(const libnet::Endpoint &peer, std::string_view data) {
    if (closing || !peer.port() || data.size()>65507) return DKNET_INVALID;
    if (pending_datagrams>=config.maximum_pending_datagrams ||
        data.size()>config.maximum_pending_bytes-pending_bytes) {
      blocked=true; return DKNET_BUSY;
    }
    if(rebind_pending)return DKNET_BUSY;
    auto *output=output_for(peer);if(!output)return DKNET_IO;
    const auto n=data.size();
    ++pending_datagrams; pending_bytes+=n;
    const auto generation=path_generation;
    auto completed=[this,n,generation,peer](int status){
      --pending_datagrams; pending_bytes-=n;
      if (closing) return;
      if (status && generation==path_generation) emit(DKNET_ERROR,0,DKNET_IO,peer);
      if (blocked) { blocked=false; emit(DKNET_WRITABLE); }
    };
    bool accepted=false;
    try {
      accepted=output->Send(peer,data,completed);
    } catch (...) { --pending_datagrams; pending_bytes-=n; throw; }
    if (!accepted) { --pending_datagrams; pending_bytes-=n; return DKNET_IO; }
    return DKNET_OK;
  }
  bool initialize(const dknet_config &input) {
    config=input;
    if (uv_loop_init(&loop)) return false;
    loop_initialized=true;
    async.data=this;
    if (uv_async_init(&loop,&async,[](uv_async_t *h){
      auto *c=static_cast<dknet_context *>(h->data);
      uint32_t bits=0;
      { std::lock_guard lock(c->gate); bits=std::exchange(c->pending_wakes,0); }
      for (uint32_t slot=0;slot<32 && !c->closing;++slot)
        if (bits&(uint32_t{1}<<slot)) c->emit(DKNET_WAKE,slot);
      if (std::exchange(c->quic_events_ready,false)) c->emit(DKNET_QUIC_EVENTS);
    })) return false;
    async_initialized=true;
    for (auto &timer:timers) {
      timer.data=this;
      if (uv_timer_init(&loop,&timer)) return false;
      ++timers_initialized;
    }
    quic_timer.data=this;
    if (uv_timer_init(&loop,&quic_timer)) return false;
    quic_initialized=true;
    receive=[this](std::string_view data,const libnet::Endpoint &peer,unsigned flags){
      if (flags&UV_UDP_PARTIAL) { emit(DKNET_ERROR,0,DKNET_IO); return; }
      emit(DKNET_DATAGRAM,0,DKNET_OK,peer,data);
    };
    receive_error=[this](int){ emit(DKNET_ERROR,0,DKNET_IO); };
    if (!socket4.Bind(&loop,"0.0.0.0",config.port,lan_paths?UV_UDP_REUSEADDR:0) || !socket4.StartReceive(receive,receive_error)) return false;
    auto local4=socket4.local_endpoint();
    if (!local4) return false;
    if (socket6.Bind(&loop,"::",local4->port(),UV_UDP_IPV6ONLY | (lan_paths?UV_UDP_REUSEADDR:0)) && !socket6.StartReceive(receive,receive_error)) socket6.Close();
    if (!quic.StartWithAlpn(*local4,socket6.local_endpoint().value_or(libnet::Endpoint{}),
      [this](const libnet::Endpoint &peer,std::string_view data){
        if (!config.send_quic) return send(peer,data)==DKNET_OK;
        const auto p=external(peer);
        sending=true;
        int32_t status=DKNET_INTERNAL;
        try { status=config.send_quic(config.user,&p,reinterpret_cast<const uint8_t *>(data.data()),data.size()); }
        catch (...) { callback_failed=true; }
        sending=false;
        return status==DKNET_OK;
      },config.maximum_connections,config.alpn)) return false;
    config.alpn=nullptr; // copied by provider; no borrowed config pointers retained
    { std::lock_guard lock(gate); wake_open=true; }
    return true;
  }
  void shutdown() {
    if (closing) return;
    { std::lock_guard lock(gate); wake_open=false; pending_wakes=0; }
    closing=true;
    quic.Stop();
    outputs.clear();
    socket4.Close(); socket6.Close();
    for (size_t i=0;i<timers_initialized;++i) {
      uv_timer_stop(&timers[i]); uv_close(reinterpret_cast<uv_handle_t *>(&timers[i]),nullptr);
    }
    if (quic_initialized) { uv_timer_stop(&quic_timer); uv_close(reinterpret_cast<uv_handle_t *>(&quic_timer),nullptr); }
    if (async_initialized) uv_close(reinterpret_cast<uv_handle_t *>(&async),nullptr);
  }
  ~dknet_context() {
    shutdown();
    if (loop_initialized) { uv_run(&loop,UV_RUN_DEFAULT); uv_loop_close(&loop); }
  }
};

extern "C" {
int32_t DKNET_CALL dknet_quic_probe(const char *alpn,const uint8_t *data,size_t n,uint32_t timeout,dknet_probe_result *out) {
  return protect([&]()->int32_t{
    if (!out || out->struct_size!=sizeof(*out) || !alpn || !*alpn || std::strlen(alpn)>255 ||
        !bytes(data,n) || !n || n>65536 || !timeout || timeout>30000) return DKNET_INVALID;
    auto r=libnet::RunQuicLoopbackProbeWithAlpn(view(data,n),std::chrono::milliseconds(timeout),alpn);
    *out={}; out->struct_size=sizeof(*out);
    out->handshake_completed=r.handshake_completed; out->stream_completed=r.bidirectional_stream_completed;
    out->udp_socket_io=r.udp_socket_io; out->early_data=r.early_data_enabled; out->packet_count=r.packet_count;
    auto copy=[](auto &dest,const std::string &s){std::memcpy(dest,s.data(),std::min(s.size(),sizeof(dest)-1));};
    copy(out->alpn,r.alpn); copy(out->cipher,r.cipher); copy(out->error,r.error);
    return DKNET_OK;
  });
}
uint32_t DKNET_CALL dknet_abi_version(void) { return DKNET_ABI_VERSION; }
const char * DKNET_CALL dknet_version(void) { return DKNET_VERSION; }
const char * DKNET_CALL dknet_backend_versions(void) { return "ngtcp2 " NGTCP2_VERSION "; " OPENSSL_VERSION_TEXT; }
int32_t DKNET_CALL dknet_endpoint_parse(const char *address,uint16_t port,dknet_endpoint *out) {
  return protect([&]()->int32_t{
    if (!address || !out) return DKNET_INVALID;
    auto p=libnet::Endpoint::Parse(address,port);
    if (!p) return DKNET_INVALID;
    *out=external(*p); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_create(const dknet_config *config,dknet_context **out) {
  if (!out) return DKNET_INVALID;
  *out=nullptr;
  return protect([&]()->int32_t{
    if (!valid_config(config)) return DKNET_INVALID;
    auto c=std::make_unique<dknet_context>();
    if (!c->initialize(*config)) return DKNET_IO;
    *out=c.release(); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_create_lan(const dknet_config *config,const dknet_lan_path_config *paths,dknet_context **out) {
  if(!out)return DKNET_INVALID;*out=nullptr;
  return protect([&]()->int32_t{
    if(!paths || paths->struct_size!=sizeof(*paths) || paths->version!=DKNET_LAN_PATH_VERSION ||
       paths->reserved || !paths->maximum_paths || paths->maximum_paths>32 ||
       (paths->interface_address && !std::memchr(paths->interface_address,0,64)))return DKNET_INVALID;
    if(!valid_config(config))return DKNET_INVALID;
    auto c=std::make_unique<dknet_context>();c->lan_paths=true;c->maximum_paths=paths->maximum_paths;
    c->explicit_interface=paths->interface_address?paths->interface_address:"";
    const int status=libnet::detail::ListLanPaths(c->explicit_interface.c_str(),c->maximum_paths,c->paths);
    // A radio-only consumer may own QUIC state without a LAN interface. It
    // still cannot send ordinary LAN datagrams until a path becomes available.
    if(status && !(status==DKNET_NOT_FOUND && c->explicit_interface.empty()))return status;
    if(!c->initialize(*config))return DKNET_IO;
    *out=c.release();return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_path_for_peer(dknet_context *c,const dknet_endpoint *peer,dknet_interface *out) {
  return protect([&]()->int32_t{
    if(!c || !out)return DKNET_INVALID;if(!c->live())return DKNET_STATE;
    const auto p=endpoint(peer);if(!p)return DKNET_INVALID;
    if(c->explicit_interface.empty() && (p->address()=="127.0.0.1" || p->address()=="::1")) {
      std::vector<libnet::detail::LanPath> local;
      const auto code=libnet::detail::ListLanPaths(p->address().c_str(),c->maximum_paths?c->maximum_paths:32,local);
      if(code)return code;
      const auto* selected=libnet::detail::SelectLanPath(local,*p);if(!selected)return DKNET_NOT_FOUND;
      *out=selected->row;return DKNET_OK;
    }
    const auto status=c->refresh_paths();if(status)return status;
    const auto *path=libnet::detail::SelectLanPath(c->paths,*p);if(!path)return DKNET_NOT_FOUND;
    *out=path->row;return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_android_initialize(void *environment,void *context) {
  return protect([&]{return libnet::detail::AndroidInitialize(environment,context);});
}
int32_t DKNET_CALL dknet_local_endpoint(dknet_context *c,uint32_t family,dknet_endpoint *out) {
  return protect([&]()->int32_t{
    if (!c || !out || (family!=4 && family!=6)) return DKNET_INVALID;
    if (!c->live()) return DKNET_STATE;
    auto p=(family==4 ? c->socket4 : c->socket6).local_endpoint();
    if (!p) return DKNET_STATE;
    *out=external(*p); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_run(dknet_context *c) {
  if (!c || !c->affine() || c->running || c->callback_depth || c->sending || c->dispatching) return DKNET_STATE;
  c->running=true; uv_run(&c->loop,UV_RUN_DEFAULT); c->running=false;
  return c->callback_failed ? DKNET_INTERNAL : DKNET_OK;
}
int32_t DKNET_CALL dknet_shutdown(dknet_context *c) {
  return protect([&]()->int32_t{
    if (!c || !c->affine() || c->sending || c->dispatching) return DKNET_STATE;
    c->shutdown(); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_destroy(dknet_context *c) {
  if (!c) return DKNET_OK;
  if (!c->affine() || c->running || c->callback_depth || c->sending || c->dispatching) return DKNET_STATE;
  delete c; return DKNET_OK;
}
int32_t DKNET_CALL dknet_wake(dknet_context *c,uint32_t slot) {
  return protect([&]()->int32_t{
    if (!c || slot>=32) return DKNET_INVALID;
    std::lock_guard lock(c->gate);
    if (!c->wake_open) return DKNET_STATE;
    c->pending_wakes|=uint32_t{1}<<slot;
    return uv_async_send(&c->async) ? DKNET_IO : DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_timer_start(dknet_context *c,uint32_t slot,uint64_t delay,uint64_t repeat) {
  if (!c || slot>=8) return DKNET_INVALID;
  if (!c->live()) return DKNET_STATE;
  return uv_timer_start(&c->timers[slot],[](uv_timer_t *h){
    auto *p=static_cast<dknet_context *>(h->data);
    p->emit(DKNET_TIMER,static_cast<uint32_t>(h-p->timers.data()));
  },delay,repeat) ? DKNET_IO : DKNET_OK;
}
int32_t DKNET_CALL dknet_timer_stop(dknet_context *c,uint32_t slot) {
  if (!c || slot>=8) return DKNET_INVALID;
  if (!c->live()) return DKNET_STATE;
  return uv_timer_stop(&c->timers[slot]) ? DKNET_IO : DKNET_OK;
}
int32_t DKNET_CALL dknet_send_datagram(dknet_context *c,const dknet_endpoint *peer,const uint8_t *data,size_t n) {
  return protect([&]()->int32_t{
    if (!c || !bytes(data,n)) return DKNET_INVALID;
    // UDP sends are permitted from the custom QUIC sender; QUIC reentry is not.
    if (!c->affine() || c->closing) return DKNET_STATE;
    auto p=endpoint(peer); if (!p) return DKNET_INVALID;
    return c->send(*p,view(data,n));
  });
}
int32_t DKNET_CALL dknet_rebind(dknet_context *c) {
  return protect([&]()->int32_t{
    if (!c || !c->live()) return DKNET_STATE;
    if (c->rebind_pending) return DKNET_BUSY;
    if(c->lan_paths) {
      // Cancel old immutable sources before publishing a new address snapshot.
      c->retire_outputs();
      const int status=c->refresh_paths(true);
      if(status){c->paths.clear();c->emit(DKNET_REBOUND,0,status);return status;}
    }
    bool ipv6=c->socket6.initialized();
    c->rebind_pending=ipv6?2:1; c->rebind_status=DKNET_OK;
    auto done=[c](int status){
      if (c->closing) return;
      if (status) c->rebind_status=DKNET_IO;
      if (!--c->rebind_pending) c->emit(DKNET_REBOUND,0,c->rebind_status);
    };
    if (!c->socket4.Rebind(done)) done(UV_EIO);
    if (ipv6 && !c->socket6.Rebind(done)) done(UV_EIO);
    return DKNET_OK;
  });
}
uint64_t DKNET_CALL dknet_quic_connect(dknet_context *c,const dknet_endpoint *peer) {
  try {
    if (!c || !c->live()) return 0;
    auto p=endpoint(peer); if (!p || !p->port()) return 0;
    auto id=c->quic.Connect(*p); c->update_tick(); return id;
  } catch (...) { return 0; }
}
int32_t DKNET_CALL dknet_quic_receive(dknet_context *c,const dknet_endpoint *peer,const uint8_t *data,size_t n) {
  return protect([&]()->int32_t{
    if (!c || !bytes(data,n)) return DKNET_INVALID;
    if (!c->live()) return DKNET_STATE;
    auto p=endpoint(peer); if (!p || !p->port()) return DKNET_INVALID;
    bool accepted=c->quic.ReceiveDatagram(view(data,n),*p); c->update_tick(); return accepted?1:0;
  });
}
int32_t DKNET_CALL dknet_quic_send(dknet_context *c,uint64_t id,const uint8_t *data,size_t n) {
  return protect([&]()->int32_t{
    if (!c || !bytes(data,n) || !n) return DKNET_INVALID;
    if (!c->live()) return DKNET_STATE;
    auto ok=c->quic.SendRecord(id,view(data,n)); c->update_tick();
    return ok?DKNET_OK:DKNET_IO;
  });
}
int32_t DKNET_CALL dknet_quic_can_queue(dknet_context *c,uint64_t id) {
  return protect([&]()->int32_t{ return c && c->live() ? (c->quic.CanQueueOptionalRecord(id)?1:0) : DKNET_STATE; });
}
int32_t DKNET_CALL dknet_quic_enable_streams(dknet_context *c,uint64_t id,uint32_t maximum) {
  return protect([&]()->int32_t{ return c && c->live() ? (c->quic.EnableStreams(id,maximum)?DKNET_OK:DKNET_STATE) : DKNET_STATE; });
}
int64_t DKNET_CALL dknet_quic_open_stream(dknet_context *c,uint64_t id) {
  try { return c && c->live() ? c->quic.OpenStream(id) : -1; } catch (...) { return -1; }
}
int32_t DKNET_CALL dknet_quic_send_stream(dknet_context *c,uint64_t id,int64_t stream,const uint8_t *data,size_t n) {
  return protect([&]()->int32_t{
    if (!c || !bytes(data,n) || !n) return DKNET_INVALID;
    if (!c->live()) return DKNET_STATE;
    auto result=c->quic.SendStreamRecord(id,stream,view(data,n)); c->update_tick();
    return result==libnet::QuicStreamSendResult::accepted ? DKNET_OK : result==libnet::QuicStreamSendResult::busy ? DKNET_BUSY : DKNET_STATE;
  });
}
int32_t DKNET_CALL dknet_quic_consume_stream(dknet_context *c,uint64_t id,int64_t stream,size_t n) {
  return protect([&]()->int32_t{ if (!c || !c->live()) return DKNET_STATE; if(n>32768) return DKNET_INVALID; c->quic.ConsumeStreamRecord(id,stream,n); return DKNET_OK; });
}
int32_t DKNET_CALL dknet_quic_reset_stream(dknet_context *c,uint64_t id,int64_t stream) {
  return protect([&]()->int32_t{ if (!c || !c->live()) return DKNET_STATE; c->quic.ResetStream(id,stream); return DKNET_OK; });
}
int32_t DKNET_CALL dknet_quic_finish_stream(dknet_context *c,uint64_t id,int64_t stream) {
  return protect([&]()->int32_t{ if (!c || !c->live()) return DKNET_STATE; c->quic.FinishStream(id,stream); return DKNET_OK; });
}
int32_t DKNET_CALL dknet_quic_close(dknet_context *c,uint64_t id,const char *detail) {
  return protect([&]()->int32_t{ if (!c || !c->live()) return DKNET_STATE; c->quic.Close(id,detail?detail:""); c->update_tick(); return DKNET_OK; });
}
int32_t DKNET_CALL dknet_quic_dispatch(dknet_context *c,dknet_quic_event_fn callback,void *user) {
  return protect([&]()->int32_t{
    if (!c || !callback) return DKNET_INVALID;
    if (!c->live() || c->dispatching) return DKNET_STATE;
    c->dispatching=true;
    c->quic_events_ready=false;
    struct Guard { dknet_context *c; ~Guard(){ c->dispatching=false; } } guard{c};
    for (const auto &e:c->quic.TakeEvents()) {
      const dknet_quic_event event{sizeof(dknet_quic_event),static_cast<uint32_t>(e.type)+1,
        e.connection_id,e.stream_id,external(e.peer),e.record.data(),e.record.size(),e.detail.c_str()};
      callback(user,&event);
    }
    return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_quic_statistics(dknet_context *c,uint64_t id,dknet_quic_stats *out) {
  return protect([&]()->int32_t{
    if (!c || !out || out->struct_size!=sizeof(*out)) return DKNET_INVALID;
    if (!c->live()) return DKNET_STATE;
    auto s=c->quic.Statistics(id); if (!s) return DKNET_STATE;
    *out={sizeof(*out),0,s->smoothed_rtt_us,s->congestion_window_bytes,s->bytes_in_flight,
      s->packets_sent,s->packets_lost,s->send_quantum_bytes,s->maximum_batch_datagrams,
      s->active_streams,s->stream_buffer_bytes}; return DKNET_OK;
  });
}
}
