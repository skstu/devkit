#include "libnet/net.h"
#include "lan_interfaces.h"
#include <uv.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <netioapi.h>
#else
#include <net/if.h>
#endif

namespace {
template<class F> int32_t protect(F&& f) noexcept {
  try { return f(); } catch (...) { return DKNET_INTERNAL; }
}
bool text(const char* p, size_t bound) { return p && std::memchr(p, 0, bound); }
uint32_t index(const char* name);
int parse(const dknet_endpoint& p, sockaddr_storage& out) {
  if (p.reserved || !text(p.address, sizeof(p.address))) return UV_EINVAL;
  int r=uv_ip4_addr(p.address,p.port,reinterpret_cast<sockaddr_in*>(&out));
  if (!r) return 0;
  const char* scope=std::strchr(p.address,'%');
  char base[64]{}; uint32_t scope_id=0;
  if (scope) {
    const auto size=static_cast<size_t>(scope-p.address);
    if (!size || !scope[1] || std::strchr(scope+1,'%')) return UV_EINVAL;
    std::memcpy(base,p.address,size);
    const auto* begin=scope+1; const auto* end=begin+std::strlen(begin);
    if (std::all_of(begin,end,[](char c){return c>='0' && c<='9';})) {
      const auto result=std::from_chars(begin,end,scope_id);
      if (result.ec!=std::errc{} || result.ptr!=end || !scope_id) return UV_EINVAL;
    } else if (!(scope_id=index(begin))) return UV_EINVAL;
  }
  auto* address=reinterpret_cast<sockaddr_in6*>(&out);
  r=uv_ip6_addr(scope?base:p.address,p.port,address);
  if (!r) address->sin6_scope_id=scope_id;
  return r;
}
dknet_endpoint external(const sockaddr* p) {
  dknet_endpoint out{};
  if (p->sa_family==AF_INET) {
    auto* v=reinterpret_cast<const sockaddr_in*>(p);
    uv_ip4_name(v,out.address,sizeof(out.address)); out.port=ntohs(v->sin_port);
  } else if (p->sa_family==AF_INET6) {
    auto* v=reinterpret_cast<const sockaddr_in6*>(p);
    uv_ip6_name(v,out.address,sizeof(out.address)); out.port=ntohs(v->sin6_port);
    if (v->sin6_scope_id) {
      const auto suffix="%"+std::to_string(v->sin6_scope_id);
      const auto n=std::strlen(out.address);
      if (n+suffix.size()<sizeof(out.address)) std::memcpy(out.address+n,suffix.c_str(),suffix.size()+1);
    }
  }
  return out;
}
uint32_t index(const char* name) {
#ifdef _WIN32
  wchar_t alias[256]{}; NET_LUID luid{}; NET_IFINDEX value=0;
  if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,alias,256) ||
      ConvertInterfaceAliasToLuid(alias,&luid)!=NO_ERROR ||
      ConvertInterfaceLuidToIndex(&luid,&value)!=NO_ERROR) return 0;
  return value;
#else
  return if_nametoindex(name);
#endif
}
// libuv's Unix multicast parser interprets a suffix as an interface name,
// while the public ABI uses portable numeric scopes. Resolve locally here;
// an explicit missing interface must never become the OS default (scope 0).
int native_interface(const char* input,std::string& output) {
  output=input?input:"";
  if (output.empty()) return 0;
  dknet_endpoint endpoint{}; sockaddr_storage address{};
  if (output.size()>=sizeof(endpoint.address)) return UV_EINVAL;
  std::memcpy(endpoint.address,output.c_str(),output.size()+1);
  const int status=parse(endpoint,address); if (status) return status;
  if (address.ss_family!=AF_INET6) return 0;
  const auto scope=reinterpret_cast<sockaddr_in6*>(&address)->sin6_scope_id;
  if (!scope) return 0;
  char name[UV_IF_NAMESIZE]{}; size_t length=sizeof(name);
  const int resolved=uv_if_indextoiid(scope,name,&length);
  if (resolved) return resolved;
  output=output.substr(0,output.find('%'))+"%"+name;
  return 0;
}
int enumerate(std::vector<dknet_interface>& result) {
  uv_interface_address_t* entries=nullptr; int count=0;
  const int status=uv_interface_addresses(&entries,&count);
  if (status) return status;
  struct Guard { uv_interface_address_t* p; int n; ~Guard(){uv_free_interface_addresses(p,n);} } guard{entries,count};
  if (count<0 || count>1024) return UV_ENOBUFS;
  std::vector<dknet_interface> output;
  for (int i=0;i<count;++i) {
    const auto& e=entries[i];
    if (!e.name || !text(e.name,256)) return UV_ENOBUFS;
    const int family=e.address.address4.sin_family;
    if (family!=AF_INET && family!=AF_INET6) continue;
    dknet_interface row{}; row.struct_size=sizeof(row);
    row.family=family==AF_INET?4:6; row.index=index(e.name);
    row.flags=e.is_internal?DKNET_INTERFACE_INTERNAL:0;
    std::memcpy(row.name,e.name,std::strlen(e.name)+1);
    if (family==AF_INET) {
      if (uv_ip4_name(&e.address.address4,row.address,sizeof(row.address)) ||
          uv_ip4_name(&e.netmask.netmask4,row.netmask,sizeof(row.netmask))) return UV_EINVAL;
      sockaddr_in broadcast=e.address.address4;
      broadcast.sin_addr.s_addr |= ~e.netmask.netmask4.sin_addr.s_addr;
      uv_ip4_name(&broadcast,row.broadcast,sizeof(row.broadcast));
      if ((ntohl(e.address.address4.sin_addr.s_addr)&0xffff0000U)==0xa9fe0000U) row.flags|=DKNET_INTERFACE_LINK_LOCAL;
    } else {
      if (uv_ip6_name(&e.address.address6,row.address,sizeof(row.address)) ||
          uv_ip6_name(&e.netmask.netmask6,row.netmask,sizeof(row.netmask))) return UV_EINVAL;
      row.scope_id=e.address.address6.sin6_scope_id;
      if (IN6_IS_ADDR_LINKLOCAL(&e.address.address6.sin6_addr)) row.flags|=DKNET_INTERFACE_LINK_LOCAL;
    }
    output.push_back(row);
  }
  std::sort(output.begin(),output.end(),[](const auto& a,const auto& b){
    int n=std::strcmp(a.name,b.name); if(n) return n<0;
    if(a.family!=b.family) return a.family<b.family;
    n=std::strcmp(a.address,b.address); if(n) return n<0;
    return a.scope_id<b.scope_id;
  });
  result=std::move(output); return 0;
}
int32_t copy(const std::vector<dknet_interface>& rows,dknet_interface* output,size_t capacity,size_t* count) {
  if (!count || (!output && capacity)) return DKNET_INVALID;
  *count=rows.size();
  if (!output && !capacity) return DKNET_OK;
  if (capacity<rows.size()) return DKNET_BUFFER_TOO_SMALL;
  std::copy(rows.begin(),rows.end(),output); return DKNET_OK;
}
}

struct dknet_lan_context {
  struct Udp;
  dknet_lan_config config{};
  std::thread::id owner=std::this_thread::get_id();
  uv_loop_t loop{}; uv_async_t async{}; uv_timer_t watch{};
  std::array<uv_timer_t,8> timers{};
  bool loop_initialized=false, async_initialized=false, watch_initialized=false;
  size_t timers_initialized=0;
  bool closing=false, running=false, callback_failed=false, wake_open=false;
  unsigned callback_depth=0;
  uint32_t pending_wakes=0; bool pending_refresh=false;
  std::mutex gate;
  std::map<uint64_t,std::unique_ptr<Udp>> sockets;
  uint64_t next_socket=0;
  size_t pending_bytes=0,pending_datagrams=0;
  int native_error=0;
  libnet::detail::InterfaceSnapshot snapshot;
  bool affine() const { return owner==std::this_thread::get_id(); }
  bool live() const { return affine() && !closing; }
  int32_t io(int status) { native_error=status; return status?DKNET_IO:DKNET_OK; }
  void emit(uint32_t type,uint64_t id=0,uint64_t request=0,int32_t status=DKNET_OK,
            int native=0,const dknet_endpoint& peer={},const uint8_t* bytes=nullptr,size_t size=0) noexcept {
    if (closing) return;
    const dknet_lan_event e{sizeof(e),type,id,request,snapshot.generation,status,native,peer,bytes,size};
    ++callback_depth;
    try { config.on_event(config.user,&e); } catch (...) { callback_failed=true; }
    --callback_depth;
    if (callback_failed) shutdown();
  }
  void refresh() noexcept {
    if (closing) return;
    try {
      std::vector<dknet_interface> rows;
      const auto status=enumerate(rows);
      if (status) { native_error=status; emit(DKNET_LAN_ERROR,0,0,DKNET_IO,status); }
      else if (snapshot.Update(std::move(rows))) emit(DKNET_LAN_INTERFACES);
    } catch (...) { emit(DKNET_LAN_ERROR,0,0,DKNET_INTERNAL); }
  }
  bool initialize(const dknet_lan_config& c) {
    config=c;
    if (io(uv_loop_init(&loop))) return false;
    loop_initialized=true; async.data=this;
    if (io(uv_async_init(&loop,&async,[](uv_async_t* h){
      auto* p=static_cast<dknet_lan_context*>(h->data);
      uint32_t bits; bool refresh;
      { std::lock_guard lock(p->gate); bits=std::exchange(p->pending_wakes,0); refresh=std::exchange(p->pending_refresh,false); }
      if (refresh) p->refresh();
      for(uint32_t slot=0;slot<32 && !p->closing;++slot)
        if(bits&(uint32_t{1}<<slot)) p->emit(DKNET_LAN_WAKE,0,slot);
    }))) return false;
    async_initialized=true;
    watch.data=this;
    if (io(uv_timer_init(&loop,&watch))) return false;
    watch_initialized=true;
    for(auto& timer:timers) {
      timer.data=this; if(io(uv_timer_init(&loop,&timer))) return false;
      ++timers_initialized;
    }
    { std::lock_guard lock(gate); wake_open=true; }
    return true;
  }
  Udp* find(uint64_t id) { auto i=sockets.find(id); return i==sockets.end()?nullptr:i->second.get(); }
  void shutdown() noexcept;
  ~dknet_lan_context();
};

struct dknet_lan_context::Udp {
  dknet_lan_context* context; uint64_t id;
  dknet_udp_config config{}; dknet_endpoint bound{};
  uv_udp_t handle{};
  bool initialized=false,closing=false,rebind=false,visible=false,receiving=false,blocked=false;
  uint32_t family=0;
  std::vector<char> receive_buffer;
  std::string outgoing_interface;
  std::vector<std::pair<std::string,std::string>> memberships;
  dknet_udp_stats stats{sizeof(stats),0,0,0,0,0,0,0};
  struct Send { uv_udp_send_t request{}; Udp* udp; uint64_t token; std::vector<char> bytes; };
  Udp(dknet_lan_context* c,uint64_t key,const dknet_udp_config& value):context(c),id(key),config(value),bound(value.bind),receive_buffer(value.maximum_receive_bytes) {}
  bool live() const { return context->live() && initialized && !closing && !rebind; }
  void emit(uint32_t type,uint64_t request=0,int status=DKNET_OK,int native=0,
            const dknet_endpoint& peer={},const uint8_t* data=nullptr,size_t size=0) {
    if(visible) context->emit(type,id,request,status,native,peer,data,size);
  }
  int options() {
    int status=uv_udp_set_multicast_ttl(&handle,static_cast<int>(config.multicast_ttl));
    if(!status) status=uv_udp_set_multicast_loop(&handle,static_cast<int>(config.multicast_loop));
    if(!status && family==4) status=uv_udp_set_broadcast(&handle,(config.flags&DKNET_UDP_BROADCAST)?1:0);
    if(!status && !outgoing_interface.empty()) {
      std::string native; status=native_interface(outgoing_interface.c_str(),native);
      if(!status) status=uv_udp_set_multicast_interface(&handle,native.c_str());
    }
    for(const auto& [group,interface_address]:memberships) if(!status) {
      std::string native; status=native_interface(interface_address.c_str(),native);
      if(!status) status=uv_udp_set_membership(&handle,group.c_str(),native.empty()?nullptr:native.c_str(),UV_JOIN_GROUP);
    }
    return status;
  }
  static void alloc(uv_handle_t* h,size_t,uv_buf_t* out) {
    auto* p=static_cast<Udp*>(h->data);
    *out=uv_buf_init(p->receive_buffer.data(),static_cast<unsigned int>(p->receive_buffer.size()));
  }
  static void receive(uv_udp_t* h,ssize_t n,const uv_buf_t* data,const sockaddr* source,unsigned flags) noexcept {
    auto* p=static_cast<Udp*>(h->data);
    if(!p->live()) return;
    if(n<0) { p->context->native_error=static_cast<int>(n); p->emit(DKNET_LAN_ERROR,0,DKNET_IO,static_cast<int>(n)); }
    else if(flags&UV_UDP_PARTIAL) { ++p->stats.dropped_datagrams; p->emit(DKNET_LAN_ERROR,0,DKNET_BUFFER_TOO_SMALL,UV_EMSGSIZE); }
    else if(source) {
      try { ++p->stats.received_datagrams; p->emit(DKNET_LAN_DATAGRAM,0,DKNET_OK,0,external(source),reinterpret_cast<const uint8_t*>(data->base),static_cast<size_t>(n)); }
      catch (...) { p->emit(DKNET_LAN_ERROR,0,DKNET_INTERNAL); }
    }
  }
  int start_receive() { return uv_udp_recv_start(&handle,alloc,receive); }
  int initialize() {
    sockaddr_storage address{}; int status=parse(bound,address);
    if(status) return status;
    family=address.ss_family==AF_INET?4:6;
    handle={}; handle.data=this;
    if((status=uv_udp_init(&context->loop,&handle))) return status;
    initialized=true;
    unsigned flags=0;
    if(config.flags&DKNET_UDP_REUSE_ADDRESS) flags|=UV_UDP_REUSEADDR;
    if(config.flags&DKNET_UDP_IPV6_ONLY) flags|=UV_UDP_IPV6ONLY;
    if(!(status=uv_udp_bind(&handle,reinterpret_cast<sockaddr*>(&address),flags))) {
      sockaddr_storage local{}; int length=sizeof(local);
      status=uv_udp_getsockname(&handle,reinterpret_cast<sockaddr*>(&local),&length);
      if(!status) bound=external(reinterpret_cast<sockaddr*>(&local));
    }
    if(!status) status=options();
    if(!status && receiving) status=start_receive();
    return status;
  }
  void close(bool retry=false) noexcept {
    if(closing) return;
    rebind=retry;
    if(!retry) closing=true;
    if(initialized && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&handle)))
      uv_close(reinterpret_cast<uv_handle_t*>(&handle),closed);
  }
  static void closed(uv_handle_t* h) noexcept {
    auto* p=static_cast<Udp*>(h->data); auto* c=p->context; const auto id=p->id;
    p->initialized=false;
    if(p->rebind && !p->closing && !c->closing) {
      p->rebind=false;
      int status=UV_ENOMEM;
      try { status=p->initialize(); } catch (...) {}
      if(!status) { p->emit(DKNET_LAN_REBOUND); return; }
      c->native_error=status; p->closing=true;
      p->emit(DKNET_LAN_REBOUND,0,DKNET_IO,status);
      if(p->initialized) { uv_close(reinterpret_cast<uv_handle_t*>(&p->handle),closed); return; }
    }
    p->emit(DKNET_LAN_CLOSED);
    c->sockets.erase(id);
  }
  static void sent(uv_udp_send_t* req,int status) noexcept {
    std::unique_ptr<Send> send(static_cast<Send*>(req->data));
    auto* p=send->udp; auto* c=p->context; const auto n=send->bytes.size();
    --c->pending_datagrams; c->pending_bytes-=n;
    --p->stats.pending_datagrams; p->stats.pending_bytes-=n;
    if(status) { ++p->stats.failed_sends; c->native_error=status; } else ++p->stats.sent_datagrams;
    // Free copied bytes before admitting another send from its completion.
    const auto token=send->token; send.reset();
    p->emit(DKNET_LAN_SENT,token,status?DKNET_IO:DKNET_OK,status);
    // A context-wide credit release wakes every live socket that saw BUSY.
    for(auto& [id,socket]:c->sockets) {
      (void)id;
      if(socket->live() && socket->blocked) { socket->blocked=false; socket->emit(DKNET_LAN_WRITABLE); }
    }
  }
};

void dknet_lan_context::shutdown() noexcept {
  if(closing) return;
  { std::lock_guard lock(gate); wake_open=false; pending_wakes=0; pending_refresh=false; }
  closing=true;
  for(auto& [id,socket]:sockets) { (void)id; socket->close(); }
  for(size_t i=0;i<timers_initialized;++i) { uv_timer_stop(&timers[i]); uv_close(reinterpret_cast<uv_handle_t*>(&timers[i]),nullptr); }
  if(watch_initialized) { uv_timer_stop(&watch); uv_close(reinterpret_cast<uv_handle_t*>(&watch),nullptr); }
  if(async_initialized) uv_close(reinterpret_cast<uv_handle_t*>(&async),nullptr);
}
dknet_lan_context::~dknet_lan_context() {
  shutdown(); if(loop_initialized) { uv_run(&loop,UV_RUN_DEFAULT); uv_loop_close(&loop); }
}

extern "C" {
uint32_t DKNET_CALL dknet_lan_version(void) { return DKNET_LAN_VERSION; }
int32_t DKNET_CALL dknet_lan_create(const dknet_lan_config* config,dknet_lan_context** out) {
  if(!out) return DKNET_INVALID; *out=nullptr;
  return protect([&]()->int32_t {
    if(!config || config->struct_size!=sizeof(*config) || config->version!=1 || config->reserved || !config->on_event ||
       !config->maximum_sockets || config->maximum_sockets>64 || !config->maximum_memberships || config->maximum_memberships>64 ||
       !config->maximum_pending_datagrams || config->maximum_pending_datagrams>65536 || !config->maximum_pending_bytes || config->maximum_pending_bytes>64*1024*1024) return DKNET_INVALID;
    auto c=std::make_unique<dknet_lan_context>(); if(!c->initialize(*config)) return DKNET_IO;
    *out=c.release(); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_lan_run(dknet_lan_context* c) {
  if(!c || !c->affine() || c->running || c->callback_depth) return DKNET_STATE;
  c->running=true; uv_run(&c->loop,UV_RUN_DEFAULT); c->running=false;
  return c->callback_failed?DKNET_INTERNAL:DKNET_OK;
}
int32_t DKNET_CALL dknet_lan_shutdown(dknet_lan_context* c) {
  if(!c || !c->affine()) return DKNET_STATE; c->shutdown(); return DKNET_OK;
}
int32_t DKNET_CALL dknet_lan_destroy(dknet_lan_context* c) {
  if(!c) return DKNET_OK;
  if(!c->affine() || c->running || c->callback_depth) return DKNET_STATE;
  delete c; return DKNET_OK;
}
int32_t DKNET_CALL dknet_lan_wake(dknet_lan_context* c,uint32_t slot) {
  if(!c || slot>=32) return DKNET_INVALID;
  return protect([&]()->int32_t { std::lock_guard lock(c->gate); if(!c->wake_open) return DKNET_STATE;
    c->pending_wakes|=uint32_t{1}<<slot; return uv_async_send(&c->async)?DKNET_IO:DKNET_OK; });
}
int32_t DKNET_CALL dknet_lan_timer_start(dknet_lan_context* c,uint32_t slot,uint64_t delay,uint64_t repeat) {
  if(!c || slot>=8) return DKNET_INVALID; if(!c->live()) return DKNET_STATE;
  return c->io(uv_timer_start(&c->timers[slot],[](uv_timer_t* h){ auto* p=static_cast<dknet_lan_context*>(h->data); p->emit(DKNET_LAN_TIMER,0,static_cast<uint64_t>(h-p->timers.data())); },delay,repeat));
}
int32_t DKNET_CALL dknet_lan_timer_stop(dknet_lan_context* c,uint32_t slot) {
  if(!c || slot>=8) return DKNET_INVALID; if(!c->live()) return DKNET_STATE; return c->io(uv_timer_stop(&c->timers[slot]));
}
int32_t DKNET_CALL dknet_interface_list(dknet_interface* output,size_t capacity,size_t* count) {
  if(!count || (!output && capacity)) return DKNET_INVALID; *count=0;
  return protect([&]()->int32_t { std::vector<dknet_interface> rows; if(enumerate(rows)) return DKNET_IO; return copy(rows,output,capacity,count); });
}
int32_t DKNET_CALL dknet_lan_watch_interfaces(dknet_lan_context* c,uint32_t interval) {
  if(!c || (interval && (interval<250 || interval>60000))) return DKNET_INVALID;
  if(!c->live()) return DKNET_STATE;
  if(!interval) return c->io(uv_timer_stop(&c->watch));
  return c->io(uv_timer_start(&c->watch,[](uv_timer_t* h){ static_cast<dknet_lan_context*>(h->data)->refresh(); },0,interval));
}
int32_t DKNET_CALL dknet_lan_refresh_interfaces(dknet_lan_context* c) {
  if(!c || !c->live()) return DKNET_STATE;
  std::lock_guard lock(c->gate); c->pending_refresh=true; return c->io(uv_async_send(&c->async));
}
int32_t DKNET_CALL dknet_lan_interfaces(dknet_lan_context* c,dknet_interface* output,size_t capacity,size_t* count,uint64_t* generation) {
  if(!c || !count || !generation || (!output && capacity)) return DKNET_INVALID;
  if(!c->live()) return DKNET_STATE;
  *generation=c->snapshot.generation;
  return copy(c->snapshot.rows,output,capacity,count);
}
int32_t DKNET_CALL dknet_lan_last_native_error(dknet_lan_context* c) {
  return c && c->affine()?c->native_error:DKNET_STATE;
}
int32_t DKNET_CALL dknet_udp_open(dknet_lan_context* c,const dknet_udp_config* config,uint64_t* out) {
  if(!out) return DKNET_INVALID; *out=0;
  return protect([&]()->int32_t {
    if(!c || !config || config->struct_size!=sizeof(*config) || config->version!=1 || config->flags&~7u ||
       !config->maximum_receive_bytes || config->maximum_receive_bytes>65536 || !config->multicast_ttl || config->multicast_ttl>255 || config->multicast_loop>1) return DKNET_INVALID;
    if(!c->live()) return DKNET_STATE;
    sockaddr_storage address{}; if(parse(config->bind,address)) return DKNET_INVALID;
    if((address.ss_family==AF_INET && config->flags&DKNET_UDP_IPV6_ONLY) || (address.ss_family==AF_INET6 && config->flags&DKNET_UDP_BROADCAST)) return DKNET_INVALID;
    if(c->sockets.size()>=c->config.maximum_sockets || c->next_socket==UINT64_MAX) return DKNET_BUSY;
    const auto id=++c->next_socket;
    auto udp=std::make_unique<dknet_lan_context::Udp>(c,id,*config); auto* p=udp.get();
    c->sockets.emplace(id,std::move(udp));
    int status;
    try { status=p->initialize(); }
    catch (...) { if(p->initialized) p->close(); else c->sockets.erase(id); throw; }
    if(status) { c->native_error=status; if(p->initialized) p->close(); else c->sockets.erase(id); return DKNET_IO; }
    p->visible=true; *out=id; return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_udp_local_endpoint(dknet_lan_context* c,uint64_t id,dknet_endpoint* out) {
  if(!c || !out) return DKNET_INVALID; if(!c->live()) return DKNET_STATE;
  auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND; if(!p->live()) return DKNET_STATE;
  *out=p->bound; return DKNET_OK;
}
int32_t DKNET_CALL dknet_udp_receive_start(dknet_lan_context* c,uint64_t id) {
  if(!c || !c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND;
  if(!p->live()) return DKNET_STATE; if(p->receiving) return DKNET_OK;
  const auto r=c->io(p->start_receive()); if(!r) p->receiving=true; return r;
}
int32_t DKNET_CALL dknet_udp_receive_stop(dknet_lan_context* c,uint64_t id) {
  if(!c || !c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND;
  if(!p->live()) return DKNET_STATE;
  const auto r=c->io(uv_udp_recv_stop(&p->handle)); if(!r) p->receiving=false; return r;
}
int32_t DKNET_CALL dknet_udp_send(dknet_lan_context* c,uint64_t id,const dknet_endpoint* peer,const uint8_t* data,size_t n,uint64_t request) {
  return protect([&]()->int32_t {
    if(!c || !peer || (!data && n) || n>65507 || !peer->port) return DKNET_INVALID;
    if(!c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND; if(!p->live()) return DKNET_STATE;
    sockaddr_storage address{}; if(parse(*peer,address) || (address.ss_family==AF_INET?4u:6u)!=p->family) return DKNET_INVALID;
    if(c->pending_datagrams>=c->config.maximum_pending_datagrams || n>c->config.maximum_pending_bytes-c->pending_bytes) { p->blocked=true; return DKNET_BUSY; }
    auto send=std::make_unique<dknet_lan_context::Udp::Send>(); send->udp=p; send->token=request;
    if(n) send->bytes.assign(reinterpret_cast<const char*>(data),reinterpret_cast<const char*>(data)+n);
    send->request.data=send.get();
    uv_buf_t buffer=uv_buf_init(send->bytes.data(),static_cast<unsigned int>(n));
    int status=uv_udp_send(&send->request,&p->handle,&buffer,1,reinterpret_cast<sockaddr*>(&address),dknet_lan_context::Udp::sent);
    if(status) return c->io(status);
    ++c->pending_datagrams; c->pending_bytes+=n; ++p->stats.pending_datagrams; p->stats.pending_bytes+=n;
    send.release(); return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_udp_membership(dknet_lan_context* c,uint64_t id,const char* group,const char* interface_address,uint32_t join) {
  return protect([&]()->int32_t {
    if(!c || !text(group,64) || (interface_address && !text(interface_address,64)) || join>1) return DKNET_INVALID;
    if(!c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND; if(!p->live()) return DKNET_STATE;
    sockaddr_storage address{}; dknet_endpoint input{}; std::memcpy(input.address,group,std::strlen(group)+1);
    if(parse(input,address) || (address.ss_family==AF_INET?4u:6u)!=p->family) return DKNET_INVALID;
    if(address.ss_family==AF_INET ? (ntohl(reinterpret_cast<sockaddr_in*>(&address)->sin_addr.s_addr)>>28)!=14 : !IN6_IS_ADDR_MULTICAST(&reinterpret_cast<sockaddr_in6*>(&address)->sin6_addr)) return DKNET_INVALID;
    if(interface_address && *interface_address) { input={}; std::memcpy(input.address,interface_address,std::strlen(interface_address)+1); if(parse(input,address) || (address.ss_family==AF_INET?4u:6u)!=p->family) return DKNET_INVALID; }
    const auto key=std::make_pair(std::string(group),std::string(interface_address?interface_address:""));
    auto i=std::find(p->memberships.begin(),p->memberships.end(),key);
    if(join && i!=p->memberships.end()) return DKNET_OK;
    if(!join && i==p->memberships.end()) return DKNET_NOT_FOUND;
    if(join && p->memberships.size()>=c->config.maximum_memberships) return DKNET_BUSY;
    std::string native; const int resolved=native_interface(interface_address,native);
    if(resolved) return c->io(resolved);
    // Allocate the retained key before applying a side effect.
    if(join) p->memberships.push_back(key);
    const int status=uv_udp_set_membership(&p->handle,group,native.empty()?nullptr:native.c_str(),join?UV_JOIN_GROUP:UV_LEAVE_GROUP);
    if(status) { if(join) p->memberships.pop_back(); return c->io(status); }
    if(!join) p->memberships.erase(i);
    return DKNET_OK;
  });
}
int32_t DKNET_CALL dknet_udp_multicast_interface(dknet_lan_context* c,uint64_t id,const char* interface_address) {
  return protect([&]()->int32_t {
    if(!c || (interface_address && !text(interface_address,64))) return DKNET_INVALID;
    if(!c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND; if(!p->live()) return DKNET_STATE;
    std::string value=interface_address?interface_address:"";
    if(!value.empty()) { dknet_endpoint input{}; sockaddr_storage address{}; std::memcpy(input.address,value.c_str(),value.size()+1); if(parse(input,address) || (address.ss_family==AF_INET?4u:6u)!=p->family) return DKNET_INVALID; }
    std::string native; const int resolved=native_interface(value.c_str(),native);
    if(resolved) return c->io(resolved);
    const auto r=c->io(uv_udp_set_multicast_interface(&p->handle,native.empty()?nullptr:native.c_str()));
    if(!r) p->outgoing_interface=std::move(value); return r;
  });
}
int32_t DKNET_CALL dknet_udp_rebind(dknet_lan_context* c,uint64_t id) {
  if(!c || !c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND;
  if(p->rebind) return DKNET_BUSY; if(!p->live()) return DKNET_STATE; p->close(true); return DKNET_OK;
}
int32_t DKNET_CALL dknet_udp_close(dknet_lan_context* c,uint64_t id) {
  if(!c || !c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND;
  p->close(); return DKNET_OK;
}
int32_t DKNET_CALL dknet_udp_statistics(dknet_lan_context* c,uint64_t id,dknet_udp_stats* out) {
  if(!c || !out || out->struct_size!=sizeof(*out)) return DKNET_INVALID;
  if(!c->live()) return DKNET_STATE; auto* p=c->find(id); if(!p) return DKNET_NOT_FOUND;
  *out=p->stats; return DKNET_OK;
}
}
