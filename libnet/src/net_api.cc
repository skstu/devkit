#include "libnet/net.h"
#include "libnet_quic.h"
#include "libnet_uv.h"
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
    const auto n=data.size();
    ++pending_datagrams; pending_bytes+=n;
    auto completed=[this,n](int status){
      --pending_datagrams; pending_bytes-=n;
      if (closing) return;
      if (status) emit(DKNET_ERROR,0,DKNET_IO);
      if (blocked) { blocked=false; emit(DKNET_WRITABLE); }
    };
    bool accepted=false;
    try {
      accepted=(peer.family()==libnet::AddressFamily::ipv6 ? socket6 : socket4).Send(peer,data,completed);
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
    auto receive=[this](std::string_view data,const libnet::Endpoint &peer,unsigned flags){
      if (flags&UV_UDP_PARTIAL) { emit(DKNET_ERROR,0,DKNET_IO); return; }
      emit(DKNET_DATAGRAM,0,DKNET_OK,peer,data);
    };
    auto error=[this](int){ emit(DKNET_ERROR,0,DKNET_IO); };
    if (!socket4.Bind(&loop,"0.0.0.0",config.port) || !socket4.StartReceive(receive,error)) return false;
    auto local4=socket4.local_endpoint();
    if (!local4) return false;
    if (socket6.Bind(&loop,"::",local4->port(),UV_UDP_IPV6ONLY) && !socket6.StartReceive(receive,error)) socket6.Close();
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
    if (!config || config->struct_size!=sizeof(*config) || config->abi_version!=DKNET_ABI_VERSION ||
        config->reserved || !config->on_event || !config->alpn ||
        !*config->alpn || std::strlen(config->alpn)>255 ||
        !config->maximum_connections || config->maximum_connections>256 ||
        !config->maximum_pending_datagrams || config->maximum_pending_datagrams>65536 ||
        !config->maximum_pending_bytes || config->maximum_pending_bytes>64*1024*1024 ||
        !config->quic_tick_ms || config->quic_tick_ms>1000) return DKNET_INVALID;
    auto c=std::make_unique<dknet_context>();
    if (!c->initialize(*config)) return DKNET_IO;
    *out=c.release(); return DKNET_OK;
  });
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
