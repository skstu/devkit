#pragma once
// Internal loop-owned TCP storage. Product framing/authentication stay in callers.
#include "libnet/net.h"
#include "libnet_uv.h"
#include <array>
#include <map>
#include <memory>
#include <functional>
#include <cstring>
#include <vector>

namespace libnet::detail {
class TcpStreams {
  struct Socket {
    TcpStreams* owner{};
    uint64_t id{};
    uv_tcp_t handle{};
    uv_connect_t connect{};
    Endpoint peer;
    bool ready=false, closing=false;
    int reason=0;
    std::array<char,16384> buffer{};
  };
  struct Write { uv_write_t req{}; Socket* socket{}; std::vector<char> data; };
  struct Listener { uv_tcp_t handle{}; TcpStreams* owner{}; bool initialized=false; };
  uv_loop_t* loop_{};
  dknet_tcp_config config_{};
  std::function<void(const dknet_tcp_event&)> emit_;
  std::map<uint64_t,std::unique_ptr<Socket>> sockets_;
  std::array<Listener,2> listeners_{};
  uv_tcp_t rejected_{};
  bool rejecting_=false, closing_=false, blocked_=false;
  uint64_t next_=1;
  size_t writes_=0, bytes_=0;
  void emit(uint32_t type,uint64_t id,const Endpoint& peer,int code=0,const char* data=nullptr,size_t size=0) {
    if(closing_)return;
    dknet_tcp_event event{};event.struct_size=sizeof(event);event.type=type;
    event.connection=id;event.status=code?DKNET_IO:DKNET_OK;event.native_status=code;
    const auto address=peer.address();
    if(address.size()<sizeof(event.peer.address))std::memcpy(event.peer.address,address.data(),address.size());
    event.peer.port=peer.port();event.data=reinterpret_cast<const uint8_t*>(data);event.size=size;
    emit_(event);
  }
  Socket* create(int family) {
    if(sockets_.size()>=config_.maximum_connections || !next_)return nullptr;
    auto s=std::make_unique<Socket>();s->owner=this;s->id=next_++;
    // Insert before uv init: allocation failure must not leave a live handle.
    auto* p=s.get();sockets_.emplace(p->id,std::move(s));
    last_error=uv_tcp_init_ex(loop_,&p->handle,family);
    if(last_error){sockets_.erase(p->id);return nullptr;}
    p->handle.data=p;p->connect.data=p;return p;
  }
  static void alloc(uv_handle_t* handle,size_t,uv_buf_t* buffer) {
    auto* s=static_cast<Socket*>(handle->data);
    *buffer=uv_buf_init(s->buffer.data(),static_cast<unsigned>(s->buffer.size()));
  }
  static void read(uv_stream_t* handle,ssize_t n,const uv_buf_t* buffer) {
    auto* s=static_cast<Socket*>(handle->data);auto* o=s->owner;
    if(s->closing)return;
    if(n<0)o->close(s->id,static_cast<int>(n));
    else if(n)o->emit(DKNET_TCP_DATA,s->id,s->peer,0,buffer->base,static_cast<size_t>(n));
  }
  void ready(Socket& s,uint32_t type) {
    s.ready=true;uv_tcp_nodelay(&s.handle,1);
    const int status=uv_read_start(reinterpret_cast<uv_stream_t*>(&s.handle),alloc,read);
    if(status){close(s.id,status);return;}
    emit(type,s.id,s.peer);
  }
  void accept(Listener& listener,int status) {
    if(closing_)return;
    if(status){last_error=status;return;}
    Socket* s=create(AF_UNSPEC);
    if(!s) {
      // One bounded rejection handle drains a full application admission budget.
      if(!rejecting_ && uv_tcp_init(loop_,&rejected_)==0) {
        rejecting_=true;rejected_.data=this;
        uv_accept(reinterpret_cast<uv_stream_t*>(&listener.handle),reinterpret_cast<uv_stream_t*>(&rejected_));
        uv_close(reinterpret_cast<uv_handle_t*>(&rejected_),[](uv_handle_t* h){static_cast<TcpStreams*>(h->data)->rejecting_=false;});
      }
      return;
    }
    status=uv_accept(reinterpret_cast<uv_stream_t*>(&listener.handle),reinterpret_cast<uv_stream_t*>(&s->handle));
    sockaddr_storage peer{};int size=sizeof(peer);
    if(!status)status=uv_tcp_getpeername(&s->handle,reinterpret_cast<sockaddr*>(&peer),&size);
    if(status){close(s->id,status);return;}
    s->peer=Endpoint::FromSockaddr(reinterpret_cast<sockaddr*>(&peer),size).value_or(Endpoint{});
    ready(*s,DKNET_TCP_ACCEPTED);
  }
public:
  int last_error=0;
  bool configured() const { return config_.on_event!=nullptr; }
  void configure(uv_loop_t* loop,const dknet_tcp_config& config,std::function<void(const dknet_tcp_event&)> emit) {
    loop_=loop;config_=config;emit_=std::move(emit);
  }
  int listen(const Endpoint& endpoint) {
    auto& l=listeners_[endpoint.family()==AddressFamily::ipv6?1:0];
    if(l.initialized)return DKNET_STATE;
    l.owner=this;last_error=uv_tcp_init(loop_,&l.handle);
    if(last_error)return DKNET_IO;
    l.initialized=true;l.handle.data=&l;
    last_error=uv_tcp_bind(&l.handle,endpoint.sockaddr_ptr(),endpoint.family()==AddressFamily::ipv6?UV_TCP_IPV6ONLY:0);
    if(!last_error)last_error=uv_listen(reinterpret_cast<uv_stream_t*>(&l.handle),static_cast<int>(config_.maximum_connections),[](uv_stream_t* h,int code){
      auto* l=static_cast<Listener*>(h->data);
      try {l->owner->accept(*l,code);}catch(...){l->owner->shutdown();}
    });
    if(last_error) {uv_close(reinterpret_cast<uv_handle_t*>(&l.handle),nullptr);return DKNET_IO;}
    return DKNET_OK;
  }
  int connect(const Endpoint& peer,uint64_t& id,const std::function<int(uv_tcp_t*)>& prepare) {
    id=0;if(sockets_.size()>=config_.maximum_connections)return DKNET_BUSY;
    auto* s=create(peer.family()==AddressFamily::ipv6?AF_INET6:AF_INET);if(!s)return DKNET_IO;
    s->peer=peer;
    last_error=prepare?prepare(&s->handle):0;
    if(!last_error)last_error=uv_tcp_connect(&s->connect,&s->handle,peer.sockaddr_ptr(),[](uv_connect_t* req,int code){
      auto* s=static_cast<Socket*>(req->data);if(s->closing)return;
      if(code)s->owner->close(s->id,code);else s->owner->ready(*s,DKNET_TCP_CONNECTED);
    });
    if(last_error){close(s->id,last_error);return DKNET_IO;}
    id=s->id;return DKNET_OK;
  }
  int send(uint64_t id,const uint8_t* data,size_t size) {
    auto it=sockets_.find(id);if(it==sockets_.end() || it->second->closing || !it->second->ready)return DKNET_STATE;
    if(size>config_.maximum_pending_bytes)return DKNET_INVALID;
    if(writes_>=config_.maximum_pending_writes || size>config_.maximum_pending_bytes-bytes_) {blocked_=true;return DKNET_BUSY;}
    auto w=std::make_unique<Write>();w->socket=it->second.get();w->data.assign(data,data+size);w->req.data=w.get();
    uv_buf_t buffer=uv_buf_init(w->data.data(),static_cast<unsigned>(size));
    last_error=uv_write(&w->req,reinterpret_cast<uv_stream_t*>(&w->socket->handle),&buffer,1,[](uv_write_t* req,int code){
      std::unique_ptr<Write> w(static_cast<Write*>(req->data));auto* s=w->socket;auto* o=s->owner;
      --o->writes_;o->bytes_-=w->data.size();
      if(code && !s->closing)o->close(s->id,code);
      o->emit(DKNET_TCP_SENT,s->id,s->peer,code,nullptr,w->data.size());
      if(o->blocked_){o->blocked_=false;o->emit(DKNET_TCP_WRITABLE,s->id,s->peer);}
    });
    if(last_error)return DKNET_IO;
    ++writes_;bytes_+=size;w.release();return DKNET_OK;
  }
  int pause(uint64_t id,bool paused) {
    auto it=sockets_.find(id);if(it==sockets_.end() || it->second->closing || !it->second->ready)return DKNET_STATE;
    auto* handle=reinterpret_cast<uv_stream_t*>(&it->second->handle);
    last_error=paused?uv_read_stop(handle):uv_read_start(handle,alloc,read);
    return last_error?DKNET_IO:DKNET_OK;
  }
  int close(uint64_t id,int reason=0) {
    auto it=sockets_.find(id);if(it==sockets_.end())return DKNET_STATE;
    auto& s=*it->second;if(s.closing)return DKNET_OK;
    s.closing=true;s.reason=reason;uv_read_stop(reinterpret_cast<uv_stream_t*>(&s.handle));
    uv_close(reinterpret_cast<uv_handle_t*>(&s.handle),[](uv_handle_t* h){
      auto* s=static_cast<Socket*>(h->data);auto* o=s->owner;
      const auto id=s->id;const auto peer=s->peer;const int code=s->reason;
      o->sockets_.erase(id);o->emit(DKNET_TCP_CLOSED,id,peer,code);
    });return DKNET_OK;
  }
  void shutdown() {
    closing_=true;
    for(auto& l:listeners_)if(l.initialized && !uv_is_closing(reinterpret_cast<uv_handle_t*>(&l.handle)))uv_close(reinterpret_cast<uv_handle_t*>(&l.handle),nullptr);
    for(auto& [id,s]:sockets_)close(id,UV_ECANCELED);
  }
};
}
