#include "lan_paths.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"LAN path check failed at %d: %s\n",__LINE__,#x);std::abort();} } while(0)
using namespace std::chrono_literals;
using libnet::detail::LanPath;
LanPath path(const char *name,const char *address,const char *mask,uint32_t flags=DKNET_INTERFACE_UP) {
  LanPath p;p.row.struct_size=sizeof(p.row);p.row.family=4;p.row.flags=flags;
  std::strcpy(p.row.name,name);std::strcpy(p.row.address,address);std::strcpy(p.row.netmask,mask);return p;
}
void selection() {
  auto wifi=path("wifi","192.168.3.17","255.255.255.0");wifi.preferred=true;
  auto ethernet=path("ethernet","10.2.0.10","255.255.255.0");
  auto vpn=path("vpn","10.8.0.2","255.255.255.0",DKNET_INTERFACE_UP|DKNET_INTERFACE_TUNNEL);
  std::vector<LanPath> paths{vpn,ethernet,wifi};
  auto selected=[&](const char *address){return libnet::detail::SelectLanPath(paths,*libnet::Endpoint::Parse(address,443));};
  CHECK(selected("192.168.3.9")->row.name==std::string("wifi")); // NAT-visible router source
  CHECK(selected("10.2.0.20")->row.name==std::string("ethernet"));
  CHECK(selected("10.8.0.20")->row.name==std::string("vpn")); // actual VPN subnet
  CHECK(selected("10.77.0.20")->row.name==std::string("wifi")); // nested private subnet, local route hint only
  CHECK(libnet::detail::SyntheticAddress("198.18.0.1"));
  CHECK(selected("198.18.0.1")==nullptr);
  CHECK(libnet::detail::SyntheticAddress("198.19.255.254"));
  CHECK(!libnet::detail::SyntheticAddress("192.168.3.17"));
  CHECK(libnet::detail::SyntheticAddress("2001:2::1"));
  // A split-default tunnel is not evidence of a reachable VPN subnet.
  std::strcpy(vpn.row.netmask,"128.0.0.0");paths={vpn,wifi};
  CHECK(selected("10.77.0.20")->row.name==std::string("wifi"));
  std::strcpy(vpn.row.netmask,"255.255.255.0");
  auto v6=wifi;v6.row.family=6;v6.row.index=14;
  std::strcpy(v6.row.address,"fe80::123");
  CHECK(libnet::detail::MatchesLanInterface(v6.row,"::%14"));
  CHECK(libnet::detail::MatchesLanInterface(v6.row,"fe80::123%14"));
  CHECK(!libnet::detail::MatchesLanInterface(v6.row,"fe80::456%14"));
  CHECK(!libnet::detail::MatchesLanInterface(v6.row,"fe80::123%15"));
  wifi.row.index=14;
  CHECK(!libnet::detail::MatchesLanInterface(wifi.row,"::%14"));
  paths={vpn};CHECK(selected("192.168.3.9")==nullptr);
  std::strcpy(vpn.row.netmask,"0.0.0.0");paths={vpn,wifi};
  CHECK(selected("192.168.3.9")->row.name==std::string("wifi")); // tunnel default cannot steal LAN
}
struct Peer {
  dknet_context *context=nullptr;dknet_endpoint remote{};std::thread thread;std::promise<dknet_endpoint> ready;
  std::atomic<unsigned> records=0,rebound=0,errors=0,guarded=0;bool client=false;uint64_t connection=0;
  static void DKNET_CALL quic(void *user,const dknet_quic_event *e) {
    auto &p=*static_cast<Peer*>(user);
    if(e->type==DKNET_QUIC_READY){p.connection=e->connection;if(p.client)CHECK(dknet_quic_send(p.context,e->connection,reinterpret_cast<const uint8_t*>("bounded-lan"),11)==DKNET_OK);}
    if(e->type==DKNET_QUIC_RECORD){CHECK(e->size==11 && !std::memcmp(e->data,"bounded-lan",11));++p.records;if(!p.client)CHECK(dknet_quic_send(p.context,e->connection,e->data,e->size)==DKNET_OK);}
    CHECK(e->type!=DKNET_QUIC_FAILED);
  }
  static void DKNET_CALL event(void *user,const dknet_event *e) {
    auto &p=*static_cast<Peer*>(user);
    if(e->type==DKNET_DATAGRAM)CHECK(dknet_quic_receive(p.context,&e->peer,e->data,e->size)>=0);
    if(e->type==DKNET_ERROR)++p.errors;
    if(e->type==DKNET_REBOUND){CHECK(e->status==DKNET_OK);++p.rebound;}
    if(e->type==DKNET_TIMER){CHECK(dknet_shutdown(p.context)==DKNET_OK);return;}
    if(e->type==DKNET_WAKE){
      if(e->slot==0){dknet_interface selected{};CHECK(dknet_path_for_peer(p.context,&p.remote,&selected)==DKNET_OK);CHECK(std::string(selected.address)=="127.0.0.1");CHECK(dknet_quic_connect(p.context,&p.remote)!=0);}
      if(e->slot==1){CHECK(dknet_rebind(p.context)==DKNET_OK);CHECK(dknet_rebind(p.context)==DKNET_BUSY);}
      if(e->slot==2){CHECK(dknet_shutdown(p.context)==DKNET_OK);return;}
      if(e->slot==3){CHECK(dknet_quic_close(p.context,p.connection,"reconnect-test")==DKNET_OK);p.connection=0;}
    }
    CHECK(dknet_quic_dispatch(p.context,quic,&p)==DKNET_OK);
  }
  dknet_endpoint start() {
    auto result=ready.get_future();
    thread=std::thread([this]{
      dknet_config c{};c.struct_size=sizeof(c);c.abi_version=1;c.maximum_connections=4;c.maximum_pending_datagrams=128;c.maximum_pending_bytes=1048576;c.quic_tick_ms=10;c.alpn="lan-path-test/1";c.user=this;c.on_event=event;
      c.send_quic=[](void* user,const dknet_endpoint* peer,const uint8_t* data,size_t size)->int32_t {
        auto& p=*static_cast<Peer*>(user);dknet_interface path{};
        CHECK(dknet_path_for_peer(p.context,peer,&path)==DKNET_STATE);++p.guarded;
        return dknet_send_datagram(p.context,peer,data,size);
      };
      dknet_lan_path_config paths{sizeof(paths),1,4,0,"127.0.0.1"};
      CHECK(dknet_create_lan(&c,&paths,&context)==DKNET_OK);
      dknet_endpoint local{};CHECK(dknet_local_endpoint(context,4,&local)==DKNET_OK);
      CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&local)==DKNET_OK);
      CHECK(dknet_timer_start(context,0,7000,0)==DKNET_OK);ready.set_value(local);
      CHECK(dknet_run(context)==DKNET_OK);CHECK(dknet_destroy(context)==DKNET_OK);
    });return result.get();
  }
};
template<class F> void await(F ready){auto deadline=std::chrono::steady_clock::now()+3s;while(!ready() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);CHECK(ready());}
int main() {
  selection();
  dknet_lan_context *lan=nullptr;
  dknet_lan_config lc{sizeof(lc),1,4,4,4,0,4096,[](void*,const dknet_lan_event*){},nullptr};
  CHECK(dknet_lan_create(&lc,&lan)==DKNET_OK);
  dknet_udp_config udp{};udp.struct_size=sizeof(udp);udp.version=1;udp.flags=DKNET_UDP_DIRECT_PATH;
  udp.maximum_receive_bytes=1200;udp.multicast_ttl=1;udp.multicast_loop=1;
  CHECK(dknet_endpoint_parse("127.0.0.1",0,&udp.bind)==DKNET_OK);
  uint64_t socket=0;CHECK(dknet_udp_open(lan,&udp,&socket)==DKNET_OK);
  CHECK(dknet_udp_multicast_interface(lan,socket,"127.0.0.1")==DKNET_OK);
  CHECK(dknet_udp_multicast_interface(lan,socket,"127.0.0.1")==DKNET_OK);
  CHECK(dknet_udp_multicast_interface(lan,socket,"203.0.113.9")==DKNET_STATE);
  // The scoped IPv6 multicast selector must not pick the same NIC's IPv4 row.
  size_t count=0;CHECK(dknet_interface_list(nullptr,0,&count)==DKNET_OK);
  std::vector<dknet_interface> rows(count);CHECK(dknet_interface_list(rows.data(),rows.size(),&count)==DKNET_OK);
  for(const auto& row:rows)if(row.family==6 && std::string(row.address)=="::1") {
    udp.flags=DKNET_UDP_DIRECT_PATH|DKNET_UDP_IPV6_ONLY;
    CHECK(dknet_endpoint_parse("::1",0,&udp.bind)==DKNET_OK);
    uint64_t v6=0;CHECK(dknet_udp_open(lan,&udp,&v6)==DKNET_OK);
    const auto selector="::%"+std::to_string(row.index);
    CHECK(dknet_udp_multicast_interface(lan,v6,selector.c_str())==DKNET_OK);break;
  }
  CHECK(dknet_udp_rebind(lan,socket)==DKNET_OK);
  CHECK(dknet_lan_shutdown(lan)==DKNET_OK && dknet_lan_destroy(lan)==DKNET_OK);
  dknet_config c{};c.struct_size=sizeof(c);c.abi_version=1;c.maximum_connections=1;c.maximum_pending_datagrams=1;c.maximum_pending_bytes=1024;c.quic_tick_ms=10;c.alpn="invalid/1";c.on_event=[](void*,const dknet_event*){};
  dknet_context *out=nullptr;dknet_lan_path_config paths{sizeof(paths),1,4,0,"203.0.113.9"};
  CHECK(dknet_create_lan(&c,&paths,&out)==DKNET_NOT_FOUND && !out);
  paths.maximum_paths=0;CHECK(dknet_create_lan(&c,&paths,&out)==DKNET_INVALID && !out);
  paths.maximum_paths=4;paths.interface_address="127.0.0.1";
  CHECK(dknet_create_lan(&c,&paths,&out)==DKNET_OK);
  dknet_endpoint loop{};CHECK(dknet_endpoint_parse("127.0.0.1",9,&loop)==DKNET_OK);
  uint8_t data[512]{};CHECK(dknet_send_datagram(out,&loop,data,sizeof(data))==DKNET_OK);
  CHECK(dknet_send_datagram(out,&loop,data,sizeof(data))==DKNET_BUSY);
  CHECK(dknet_shutdown(out)==DKNET_OK && dknet_destroy(out)==DKNET_OK);
  struct Budget { dknet_context* context=nullptr; dknet_endpoint peer{}; bool recovered=false; } budget;
  c.maximum_pending_bytes=32; c.user=&budget;
  c.on_event=[](void* user,const dknet_event* event) {
    if(event->type!=DKNET_TIMER)return;
    auto& budget=*static_cast<Budget*>(user);
    const uint8_t bytes[32]{};
    CHECK(dknet_send_datagram(budget.context,&budget.peer,bytes,sizeof(bytes))==DKNET_OK);
    CHECK(dknet_send_datagram(budget.context,&budget.peer,bytes,1)==DKNET_BUSY);
    budget.recovered=true;
    CHECK(dknet_shutdown(budget.context)==DKNET_OK);
  };
  CHECK(dknet_create_lan(&c,&paths,&out)==DKNET_OK); budget.context=out;
  dknet_endpoint local{};CHECK(dknet_local_endpoint(out,4,&local)==DKNET_OK);
  CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&budget.peer)==DKNET_OK);
  const uint8_t bytes[32]{};
  CHECK(dknet_send_datagram(out,&budget.peer,bytes,sizeof(bytes))==DKNET_OK);
  CHECK(dknet_rebind(out)==DKNET_OK);
  CHECK(dknet_timer_start(out,0,20,0)==DKNET_OK);
  CHECK(dknet_run(out)==DKNET_OK && budget.recovered);
  CHECK(dknet_destroy(out)==DKNET_OK);

  paths.maximum_paths=32;paths.interface_address=nullptr;
  CHECK(dknet_create_lan(&c,&paths,&out)==DKNET_OK);
  dknet_interface actual{};CHECK(dknet_path_for_peer(out,&loop,&actual)==DKNET_OK);
  CHECK(actual.flags&DKNET_INTERFACE_INTERNAL);
  CHECK(dknet_shutdown(out)==DKNET_OK && dknet_destroy(out)==DKNET_OK);
  Peer a,b;a.client=true;auto aa=a.start(),bb=b.start();a.remote=bb;b.remote=aa;
  CHECK(dknet_wake(a.context,0)==DKNET_OK);await([&]{return a.records==1 && b.records==1;});
  CHECK(dknet_wake(a.context,3)==DKNET_OK && dknet_wake(b.context,3)==DKNET_OK);std::this_thread::sleep_for(40ms);
  CHECK(dknet_wake(a.context,1)==DKNET_OK && dknet_wake(b.context,1)==DKNET_OK);
  await([&]{return a.rebound==1 && b.rebound==1;});
  CHECK(dknet_wake(a.context,0)==DKNET_OK);await([&]{return a.records==2 && b.records==2;});
  CHECK(!a.errors && !b.errors && a.guarded && b.guarded);CHECK(dknet_wake(a.context,2)==DKNET_OK && dknet_wake(b.context,2)==DKNET_OK);
  a.thread.join();b.thread.join();
  std::puts("PASS: NAT-visible endpoint selection, VPN subnet/default distinction, immutable bound UDP, QUIC records, bounded admission, rebind/reconnect and invalid path rejection");
}
