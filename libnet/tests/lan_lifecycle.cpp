#include "libnet/net.h"
#include "lan_interfaces.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)
using namespace std::chrono_literals;
static int32_t literal_endpoint(const char* address,uint16_t port,dknet_endpoint* out) {
  if(std::strlen(address)>=sizeof(out->address)) return DKNET_INVALID;
  *out={}; std::memcpy(out->address,address,std::strlen(address)+1); out->port=port; return DKNET_OK;
}

struct Lan {
  dknet_lan_context* context=nullptr;
  std::function<void(const dknet_lan_event&)> callback;
  explicit Lan(uint32_t sockets=8,uint32_t count=8,size_t bytes=256) {
    dknet_lan_config config{}; config.struct_size=sizeof(config); config.version=1;
    config.maximum_sockets=sockets; config.maximum_memberships=2;
    config.maximum_pending_datagrams=count; config.maximum_pending_bytes=bytes;
    config.user=this; config.on_event=[](void* user,const dknet_lan_event* e) {
      static_cast<Lan*>(user)->callback(*e);
    };
    CHECK(dknet_lan_create(&config,&context)==DKNET_OK);
  }
  ~Lan() { CHECK(dknet_lan_destroy(context)==DKNET_OK); }
  uint64_t open(const char* address="127.0.0.1",uint16_t port=0,uint32_t flags=0) {
    auto config=options(address,port,flags); uint64_t id=0;
    CHECK(dknet_udp_open(context,&config,&id)==DKNET_OK && id); return id;
  }
  static dknet_udp_config options(const char* address,uint16_t port,uint32_t flags) {
    dknet_udp_config config{}; config.struct_size=sizeof(config); config.version=1;
    config.flags=flags; config.maximum_receive_bytes=64; config.multicast_ttl=1; config.multicast_loop=1;
    CHECK(literal_endpoint(address,port,&config.bind)==DKNET_OK); return config;
  }
  dknet_endpoint local(uint64_t id) {
    dknet_endpoint e{}; CHECK(dknet_udp_local_endpoint(context,id,&e)==DKNET_OK); return e;
  }
  void timer(uint32_t slot,uint64_t delay) { CHECK(dknet_lan_timer_start(context,slot,delay,0)==DKNET_OK); }
  void run() { CHECK(dknet_lan_run(context)==DKNET_OK); }
};
static void snapshots() {
  libnet::detail::InterfaceSnapshot snapshot;
  dknet_interface row{}; row.struct_size=sizeof(row); row.family=6; row.index=5; row.scope_id=5;
  std::strcpy(row.name,"test0"); std::strcpy(row.address,"fe80::1"); std::strcpy(row.netmask,"ffff:ffff:ffff:ffff::");
  CHECK(snapshot.Update({row}) && snapshot.generation==1);
  CHECK(!snapshot.Update({row}) && snapshot.generation==1);
  row.scope_id=6; CHECK(snapshot.Update({row}));
  row.index=6; CHECK(snapshot.Update({row}));
  row.flags=DKNET_INTERFACE_LINK_LOCAL; CHECK(snapshot.Update({row}));
  std::strcpy(row.address,"fe80::2"); CHECK(snapshot.Update({row}));
  std::strcpy(row.netmask,"ffff:ffff:ffff::"); CHECK(snapshot.Update({row}));
  CHECK(snapshot.Update({}) && snapshot.rows.empty());
  CHECK(!snapshot.Update({}) && snapshot.generation==7);
}
static void broadcast_and_ipv6() {
  size_t count=0; CHECK(dknet_interface_list(nullptr,0,&count)==DKNET_OK);
  std::vector<dknet_interface> rows(count); CHECK(dknet_interface_list(rows.data(),rows.size(),&count)==DKNET_OK);
  const dknet_interface* chosen=nullptr;
  for(const auto& row:rows) if(row.family==4 && !(row.flags&DKNET_INTERFACE_INTERNAL) && row.broadcast[0] && std::strcmp(row.broadcast,row.address)) { chosen=&row; break; }
  CHECK(chosen); // This fixture requires one local broadcast-capable IPv4 address.
  Lan lan; auto receiver=lan.open("0.0.0.0"); auto sender=lan.open(chosen->address,0,DKNET_UDP_BROADCAST);
  auto ipv6=lan.open("::1",0,DKNET_UDP_IPV6_ONLY); auto remote6=lan.local(ipv6);
  dknet_endpoint broadcast{}; CHECK(literal_endpoint(chosen->broadcast,lan.local(receiver).port,&broadcast)==DKNET_OK);
  CHECK(dknet_udp_receive_start(lan.context,receiver)==DKNET_OK);
  CHECK(dknet_udp_receive_start(lan.context,ipv6)==DKNET_OK);
  unsigned received4=0,received6=0,sent=0;
  lan.callback=[&](const auto& e) {
    if(e.type==DKNET_LAN_DATAGRAM) {
      CHECK(e.size==1 && e.data[0]=='x');
      if(e.socket==receiver) ++received4; else { CHECK(e.socket==ipv6); ++received6; }
    } else if(e.type==DKNET_LAN_SENT) { CHECK(e.status==DKNET_OK); ++sent; }
    else if(e.type==DKNET_LAN_TIMER) {
      std::fprintf(stderr,"broadcast=%u ipv6=%u sent=%u\n",received4,received6,sent);
      CHECK(false);
      CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
    } else CHECK(false);
    if(received4==1 && received6==1 && sent==2) CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
  };
  CHECK(dknet_udp_send(lan.context,sender,&broadcast,reinterpret_cast<const uint8_t*>("x"),1,0)==DKNET_OK);
  CHECK(dknet_udp_send(lan.context,ipv6,&remote6,reinterpret_cast<const uint8_t*>("x"),1,0)==DKNET_OK);
  CHECK(dknet_udp_send(lan.context,ipv6,&broadcast,reinterpret_cast<const uint8_t*>("x"),1,0)==DKNET_INVALID);
  lan.timer(0,5000); lan.run();
}
static void ipv6_scopes() {
  size_t count=0; CHECK(dknet_interface_list(nullptr,0,&count)==DKNET_OK);
  std::vector<dknet_interface> rows(count); CHECK(dknet_interface_list(rows.data(),rows.size(),&count)==DKNET_OK);
  const dknet_interface* loopback=nullptr;
  for(const auto& row:rows) if(row.family==6 && row.index && (row.flags&DKNET_INTERFACE_INTERNAL)) { loopback=&row; break; }
  CHECK(loopback);
  Lan lan; auto id=lan.open("::",0,DKNET_UDP_IPV6_ONLY);
  const auto numeric=std::string("::%")+std::to_string(loopback->index);
  CHECK(dknet_udp_multicast_interface(lan.context,id,numeric.c_str())==DKNET_OK);
  CHECK(dknet_udp_multicast_interface(lan.context,id,"::%4294967295")!=DKNET_OK);
  CHECK(dknet_udp_multicast_interface(lan.context,id,"::%0")==DKNET_INVALID);
  CHECK(dknet_udp_multicast_interface(lan.context,id,"::%4294967296")==DKNET_INVALID);
  // Missing/zero explicit scopes must not silently join on the default NIC.
  CHECK(dknet_udp_membership(lan.context,id,"ff02::fb","::%4294967295",1)!=DKNET_OK);
  CHECK(dknet_udp_membership(lan.context,id,"ff02::fb","::%0",1)==DKNET_INVALID);
}
static void limits_failure_and_close() {
  Lan lan(2,2,4); auto a=lan.open(); auto remote=lan.local(a);
  auto conflict=Lan::options("127.0.0.1",remote.port,0); uint64_t bad=99;
  CHECK(dknet_udp_open(lan.context,&conflict,&bad)==DKNET_IO && bad==0);
  CHECK(dknet_lan_last_native_error(lan.context)<0);
  auto config=Lan::options("127.0.0.1",0,0);
  CHECK(dknet_udp_open(lan.context,&config,&bad)==DKNET_BUSY && bad==0); // failed handle is draining, still bounded
  CHECK(dknet_udp_send(lan.context,a,&remote,reinterpret_cast<const uint8_t*>("1234"),4,1)==DKNET_OK);
  CHECK(dknet_udp_send(lan.context,a,&remote,reinterpret_cast<const uint8_t*>("x"),1,2)==DKNET_BUSY); // byte bound
  CHECK(dknet_udp_rebind(lan.context,a)==DKNET_OK);
  CHECK(dknet_udp_close(lan.context,a)==DKNET_OK); // close cancels pending rebind
  unsigned sent=0,closed=0;
  uint64_t replacement=0;
  lan.callback=[&](const auto& e) {
    if(e.type==DKNET_LAN_SENT) ++sent;
    else if(e.type==DKNET_LAN_CLOSED) { CHECK(e.socket==a && sent==1); ++closed; CHECK(dknet_lan_wake(lan.context,0)==DKNET_OK); }
    else if(e.type==DKNET_LAN_WAKE) {
      CHECK(closed==1);
      replacement=lan.open(); CHECK(replacement>a);
      CHECK(dknet_udp_close(lan.context,a)==DKNET_NOT_FOUND);
      CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
    } else CHECK(false); // especially no REBOUND after close
  };
  lan.run(); CHECK(replacement && closed==1);
}
static void failed_rebind_is_truthful() {
  Lan lan; const auto a=lan.open(); const auto address=lan.local(a);
  CHECK(dknet_udp_rebind(lan.context,a)==DKNET_OK);
  // uv_close releases the descriptor before its callback. Occupy that exact
  // port in the same isolated fixture before the replacement attempts to bind.
  const auto occupied=lan.open("127.0.0.1",address.port);
  unsigned failures=0,closed=0;
  lan.callback=[&](const auto& e) {
    if(e.type==DKNET_LAN_REBOUND) {
      CHECK(e.socket==a && e.status==DKNET_IO && e.native_status<0); ++failures;
      CHECK(dknet_udp_close(lan.context,a)==DKNET_OK);
    } else if(e.type==DKNET_LAN_CLOSED) {
      CHECK(e.socket==a && failures==1); ++closed;
      CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
    } else CHECK(false);
  };
  (void)occupied; lan.run(); CHECK(failures==1 && closed==1);
}
static void shutdown_silences_and_callback_failure() {
  for(unsigned i=0;i<25;++i) {
    Lan lan; auto id=lan.open(); auto remote=lan.local(id);
    lan.callback=[](const auto&){CHECK(false);};
    CHECK(dknet_udp_receive_start(lan.context,id)==DKNET_OK);
    CHECK(dknet_udp_send(lan.context,id,&remote,reinterpret_cast<const uint8_t*>("x"),1,0)==DKNET_OK);
    CHECK(dknet_udp_rebind(lan.context,id)==DKNET_OK);
    CHECK(dknet_lan_refresh_interfaces(lan.context)==DKNET_OK);
    CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
    CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK);
    CHECK(dknet_lan_wake(lan.context,0)==DKNET_STATE);
    lan.run();
  }
  Lan lan; lan.callback=[](const auto&){throw std::runtime_error("host failure");};
  CHECK(dknet_lan_wake(lan.context,0)==DKNET_OK);
  CHECK(dknet_lan_run(lan.context)==DKNET_INTERNAL);
}
static void affinity_and_wake() {
  std::promise<dknet_lan_context*> ready;
  std::promise<void> joined;
  auto future=ready.get_future(); auto producers=joined.get_future();
  std::atomic<unsigned> wakes=0;
  std::thread owner([&] {
    Lan lan; lan.callback=[&](const auto& e){
      if(e.type==DKNET_LAN_WAKE) ++wakes;
      else { CHECK(e.type==DKNET_LAN_TIMER); CHECK(dknet_lan_shutdown(lan.context)==DKNET_OK); }
    };
    lan.timer(0,100); ready.set_value(lan.context); lan.run(); producers.wait();
  });
  auto* c=future.get();
  CHECK(dknet_lan_refresh_interfaces(c)==DKNET_STATE);
  CHECK(dknet_lan_destroy(c)==DKNET_STATE);
  CHECK(dknet_lan_shutdown(c)==DKNET_STATE);
  unsigned requests=0;
  while(dknet_lan_wake(c,31)==DKNET_OK) { ++requests; std::this_thread::sleep_for(100us); }
  CHECK(requests>0); joined.set_value(); owner.join(); CHECK(wakes>0);
}
int main() {
  snapshots(); broadcast_and_ipv6(); ipv6_scopes(); limits_failure_and_close();
  failed_rebind_is_truthful(); shutdown_silences_and_callback_failure(); affinity_and_wake();
  std::puts("LAN lifecycle: broadcast/IPv6, snapshot changes, queue/socket budgets, failure drain, close/rebind, shutdown and affinity passed");
}
