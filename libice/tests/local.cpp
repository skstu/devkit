#include <libice/ice.h>
#include <juice/juice.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Clock=std::chrono::steady_clock;
void check(bool x,const char *what) { if(!x) throw std::runtime_error(what); }
struct Peer {
  dkice_context *c=nullptr;
  std::atomic<unsigned> wakes{0};
  std::vector<std::string> received;
  ~Peer() { if(c) dkice_destroy(c); }
  static void wake(void *p) {
    auto &s=*static_cast<Peer *>(p); ++s.wakes;
    // Guard must reject even synchronous provider callbacks before checking thread affinity.
    if(dkice_stop(nullptr)!=DKICE_BUSY) std::abort();
  }
  static void event(void *p,const dkice_event *e) {
    auto &s=*static_cast<Peer *>(p);
    check(dkice_destroy(s.c)==DKICE_BUSY,"reentrant destroy");
    check(dkice_dispatch(s.c,event,p)==DKICE_BUSY,"reentrant dispatch");
    if(e->type==DKICE_EVENT_DATAGRAM) s.received.emplace_back(reinterpret_cast<const char *>(e->data),e->size);
  }
  dkice_status status() { dkice_status s{};s.struct_size=sizeof(s);check(dkice_get_status(c,&s)==0,"status");return s; }
  void drain() { check(dkice_dispatch(c,event,this)>=0,"dispatch"); }
  std::string description() {
    size_t count=0;check(dkice_local_description(c,nullptr,0,&count)==DKICE_BUFFER,"description size");
    std::string s(count,'\0');check(dkice_local_description(c,s.data(),count,&count)==0,"description");s.resize(count-1);return s;
  }
  void create(uint16_t stun) {
    dkice_config v{};v.struct_size=sizeof(v);v.abi_version=1;v.address_family=4;v.bind_address="127.0.0.1";
    v.wake=wake;v.user=this;
    if(stun){v.stun_address="127.0.0.1";v.stun_port=stun;}
    check(dkice_create(&v,&c)==0,"create");
  }
};
template<class F> void until(Peer &a,Peer &b,F done) {
  const auto end=Clock::now()+std::chrono::seconds(5);
  while(Clock::now()<end) {a.drain();b.drain();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  throw std::runtime_error("local ICE timeout");
}
void exchange(uint16_t stun) {
  Peer a,b;a.create(stun);b.create(stun);
  std::thread wrong([&]{check(dkice_stop(a.c)==DKICE_THREAD,"wrong thread");});wrong.join();
  check(dkice_gather(a.c)==0,"gather offer");
  until(a,b,[&]{return a.status().gathering_complete;});
  const auto offer=a.description();
  const std::string mixed=offer+"a=candidate:99 1 UDP 1 127.0.0.1 3478 typ relay\r\n";
  check(dkice_remote_description(b.c,mixed.data(),mixed.size())==DKICE_INVALID,"relay mixed offer rejected");
  check(dkice_remote_description(b.c,offer.data(),offer.size())==0,"remote offer");
  check(dkice_gather(b.c)==0,"gather answer");
  until(a,b,[&]{return b.status().gathering_complete;});
  const auto answer=b.description();check(dkice_remote_description(a.c,answer.data(),answer.size())==0,"remote answer");
  until(a,b,[&]{return a.status().path_allowed && b.status().path_allowed;});
  check(!a.status().relayed && !b.status().relayed,"direct path");
  const uint8_t bytes[]={0,1,2,0,255};
  check(dkice_send(a.c,bytes,sizeof(bytes))==0,"send a");check(dkice_send(b.c,bytes,sizeof(bytes))==0,"send b");
  until(a,b,[&]{return !a.received.empty()&&!b.received.empty();});
  check(a.received.front()==std::string(reinterpret_cast<const char *>(bytes),sizeof(bytes)) && a.received==b.received,"binary data integrity");
  check(dkice_stop(a.c)==0 && dkice_stop(b.c)==0,"stop");
  const auto aw=a.wakes.load(),bw=b.wakes.load();std::this_thread::sleep_for(std::chrono::milliseconds(30));
  check(a.wakes==aw&&b.wakes==bw,"worker drained on stop");
}
int main() {
  try {
    exchange(0);exchange(0);
    juice_server_config_t config{};config.bind_address="127.0.0.1";config.external_address="127.0.0.1";
    auto *server=juice_server_create(&config);check(server,"loopback STUN fixture");
    try {exchange(juice_server_get_port(server));} catch(...){juice_server_destroy(server);throw;}
    juice_server_destroy(server);
    std::cout<<"ICE host-only and loopback STUN exchange, binary datagrams, policy and lifecycle passed\n";
  } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
