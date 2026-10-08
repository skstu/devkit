#include "libnet/net.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)
using namespace std::chrono_literals;
struct Peer {
  dknet_context *context=nullptr;
  std::thread thread;
  std::promise<dknet_endpoint> ready;
  dknet_endpoint remote{};
  uint64_t connection=0;
  std::atomic<unsigned> ticks=0, records=0, streams=0, wakes=0, closed=0;
  bool client=false;
  static void DKNET_CALL quic(void *user,const dknet_quic_event *e) {
    auto &p=*static_cast<Peer *>(user);
    CHECK(dknet_run(p.context)==DKNET_STATE);
    if(e->type==DKNET_QUIC_READY) {
      p.connection=e->connection;
      CHECK(dknet_quic_enable_streams(p.context,p.connection,4)==DKNET_OK);
      if(p.client) CHECK(dknet_quic_send(p.context,p.connection,reinterpret_cast<const uint8_t *>("record"),6)==DKNET_OK);
    } else if(e->type==DKNET_QUIC_RECORD) {
      CHECK(e->size==6 && std::memcmp(e->data,"record",6)==0);
      if(!p.client) CHECK(dknet_quic_send(p.context,p.connection,e->data,e->size)==DKNET_OK);
      else {
        auto stream=dknet_quic_open_stream(p.context,p.connection); CHECK(stream>=0);
        CHECK(dknet_quic_send_stream(p.context,p.connection,stream,reinterpret_cast<const uint8_t *>("stream"),6)==DKNET_OK);
      }
      ++p.records;
    } else if(e->type==DKNET_QUIC_STREAM_RECORD) {
      CHECK(e->size==6 && std::memcmp(e->data,"stream",6)==0);
      CHECK(dknet_quic_consume_stream(p.context,p.connection,e->stream,e->size)==DKNET_OK);
      if(!p.client) CHECK(dknet_quic_send_stream(p.context,p.connection,e->stream,e->data,e->size)==DKNET_OK);
      ++p.streams;
    } else if(e->type==DKNET_QUIC_CLOSED) {
      ++p.closed;
    } else if(e->type==DKNET_QUIC_FAILED) {
      std::fprintf(stderr,"QUIC: %s\n",e->detail); CHECK(false);
    }
  }
  static void DKNET_CALL event(void *user,const dknet_event *e) {
    auto &p=*static_cast<Peer *>(user);
    if(e->type==DKNET_DATAGRAM) CHECK(dknet_quic_receive(p.context,&e->peer,e->data,e->size)>=0);
    if(e->type==DKNET_QUIC_TICK) ++p.ticks;
    if(e->type==DKNET_WAKE) {
      ++p.wakes;
      if(e->slot==0) CHECK(dknet_quic_connect(p.context,&p.remote)!=0);
      if(e->slot==1) { CHECK(dknet_quic_close(p.context,p.connection,"test close")==DKNET_OK); p.connection=0; return; }
      if(e->slot==2) { CHECK(dknet_shutdown(p.context)==DKNET_OK); return; }
    }
    CHECK(e->type!=DKNET_ERROR && e->type!=DKNET_TIMER);
    CHECK(dknet_quic_dispatch(p.context,quic,&p)==DKNET_OK);
  }
  dknet_endpoint start() {
    auto future=ready.get_future();
    thread=std::thread([this]{
      dknet_config config{};
      config.struct_size=sizeof(config); config.abi_version=DKNET_ABI_VERSION;
      config.maximum_connections=8; config.maximum_pending_datagrams=256;
      config.maximum_pending_bytes=1024*1024; config.quic_tick_ms=10;
      config.alpn="independent-example/1"; config.on_event=event; config.user=this;
      CHECK(dknet_create(&config,&context)==DKNET_OK);
      dknet_endpoint local{}; CHECK(dknet_local_endpoint(context,4,&local)==DKNET_OK);
      CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&local)==DKNET_OK);
      CHECK(dknet_timer_start(context,0,5000,0)==DKNET_OK);
      ready.set_value(local);
      CHECK(dknet_run(context)==DKNET_OK);
      CHECK(dknet_destroy(context)==DKNET_OK);
    });
    return future.get();
  }
};
template<class F> void await(F predicate) {
  auto deadline=std::chrono::steady_clock::now()+4s;
  while(!predicate() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(2ms);
  CHECK(predicate());
}
int main() {
  // Every lifecycle remains on its creating worker, with a caller-owned join gate.
  struct Idle {
    std::promise<dknet_context *> ready;
    std::promise<void> producers_done;
    dknet_context *context=nullptr;
    std::atomic<unsigned> ticks=0, wakes=0;
  } idle;
  auto f=idle.ready.get_future();
  auto producers_done=idle.producers_done.get_future();
  std::thread owner([&]{
    dknet_config c{}; c.struct_size=sizeof(c); c.abi_version=1; c.maximum_connections=2;
    c.maximum_pending_datagrams=4; c.maximum_pending_bytes=4096; c.quic_tick_ms=10;
    c.alpn="idle/1"; c.user=&idle;
    c.on_event=[](void *u,const dknet_event *e){
      auto &i=*static_cast<Idle *>(u);
      if(e->type==DKNET_QUIC_TICK)++i.ticks;
      if(e->type==DKNET_WAKE)++i.wakes;
      if(e->type==DKNET_TIMER) CHECK(dknet_shutdown(i.context)==DKNET_OK);
    };
    CHECK(dknet_create(&c,&idle.context)==DKNET_OK);
    CHECK(dknet_timer_start(idle.context,0,150,0)==DKNET_OK);
    idle.ready.set_value(idle.context);
    CHECK(dknet_run(idle.context)==DKNET_OK);
    producers_done.wait();
    CHECK(dknet_shutdown(idle.context)==DKNET_OK);
    CHECK(dknet_destroy(idle.context)==DKNET_OK);
  });
  auto *ctx=f.get();
  CHECK(dknet_timer_start(ctx,1,1,0)==DKNET_STATE);
  CHECK(dknet_destroy(ctx)==DKNET_STATE);
  unsigned attempts=0;
  while(dknet_wake(ctx,3)==DKNET_OK) { ++attempts; std::this_thread::sleep_for(50us); }
  CHECK(attempts>0);
  idle.producers_done.set_value();
  owner.join(); CHECK(idle.ticks==0 && idle.wakes>0);
  Peer a,b; a.client=true; auto aa=a.start(); auto bb=b.start(); a.remote=bb; b.remote=aa;
  std::this_thread::sleep_for(100ms); CHECK(a.ticks==0 && b.ticks==0);
  for(unsigned round=1;round<=2;++round) {
    CHECK(dknet_wake(a.context,0)==DKNET_OK);
    await([&]{return a.streams==round && b.streams==round;});
    CHECK(a.records==round && b.records==round);
    CHECK(dknet_wake(a.context,1)==DKNET_OK); CHECK(dknet_wake(b.context,1)==DKNET_OK);
    await([&]{return a.closed==round && b.closed==round;});
    std::this_thread::sleep_for(30ms); auto ta=a.ticks.load(),tb=b.ticks.load();
    std::this_thread::sleep_for(100ms); CHECK(a.ticks==ta && b.ticks==tb);
  }
  CHECK(dknet_wake(a.context,2)==DKNET_OK); CHECK(dknet_wake(b.context,2)==DKNET_OK);
  a.thread.join(); b.thread.join();
  // Context destruction must be on the owner; Peer handles cleanup there.
  std::puts("QUIC record/stream, wake, affinity and idle scheduling passed");
}
