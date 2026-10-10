#include "libnet/net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)

static void completion_and_close() {
  for(unsigned mode=0;mode<5;++mode) {
    struct State { dknet_lan_context* context=nullptr; uint64_t socket=0; unsigned sent=0, received=0; bool closed=false; unsigned mode=0; } s;
    s.mode=mode;
    dknet_lan_config c{}; c.struct_size=sizeof(c); c.version=DKNET_LAN_VERSION;
    c.maximum_sockets=1; c.maximum_memberships=1;
    c.maximum_pending_datagrams=8; c.maximum_pending_bytes=64; c.user=&s;
    c.on_event=[](void* user,const dknet_lan_event* e) {
      auto& s=*static_cast<State*>(user);
      if(e->type==DKNET_LAN_SENT) {
        CHECK(e->request==s.sent);
        const bool successful=!s.mode || (s.mode>=3 && !s.sent);
        CHECK(e->status==(successful?DKNET_OK:DKNET_IO));
        ++s.sent;
        if(s.sent==1 && s.mode==3) CHECK(dknet_udp_close(s.context,s.socket)==DKNET_OK);
        if(s.sent==1 && s.mode==4) CHECK(dknet_udp_rebind(s.context,s.socket)==DKNET_OK);
      }
      else if(e->type==DKNET_LAN_DATAGRAM) { CHECK(e->size==1 && e->data[0]==s.received); ++s.received; }
      else if(e->type==DKNET_LAN_CLOSED) { CHECK(s.sent==8); s.closed=true; CHECK(dknet_lan_shutdown(s.context)==DKNET_OK); }
      else if(e->type==DKNET_LAN_REBOUND) { CHECK((s.mode==2 || s.mode==4) && s.sent==8 && e->status==DKNET_OK); CHECK(dknet_udp_close(s.context,s.socket)==DKNET_OK); }
      else if(e->type==DKNET_LAN_TIMER) { CHECK(false); }
      if(!s.closed && s.sent==8 && s.received==8) CHECK(dknet_udp_close(s.context,s.socket)==DKNET_OK);
    };
    CHECK(dknet_lan_create(&c,&s.context)==DKNET_OK);
    dknet_udp_config socket{}; socket.struct_size=sizeof(socket); socket.version=DKNET_LAN_VERSION;
    socket.maximum_receive_bytes=64; socket.multicast_ttl=1; socket.multicast_loop=1;
    std::strcpy(socket.bind.address,"127.0.0.1");
    CHECK(dknet_udp_open(s.context,&socket,&s.socket)==DKNET_OK);
    dknet_endpoint local{}; CHECK(dknet_udp_local_endpoint(s.context,s.socket,&local)==DKNET_OK);
    CHECK(dknet_udp_receive_start(s.context,s.socket)==DKNET_OK);
    for(uint8_t i=0;i<8;++i) CHECK(dknet_udp_send(s.context,s.socket,&local,&i,1,i)==DKNET_OK);
    uint8_t extra=9; CHECK(dknet_udp_send(s.context,s.socket,&local,&extra,1,9)==DKNET_BUSY);
    if(mode==1) CHECK(dknet_udp_close(s.context,s.socket)==DKNET_OK);
    if(mode==2) CHECK(dknet_udp_rebind(s.context,s.socket)==DKNET_OK);
    CHECK(dknet_lan_timer_start(s.context,0,1000,0)==DKNET_OK);
    CHECK(dknet_lan_run(s.context)==DKNET_OK);
    CHECK(s.closed && s.sent==8 && (mode || s.received==8));
    CHECK(dknet_lan_destroy(s.context)==DKNET_OK);
  }
}

static void callback_refill_fairness() {
  struct State {
    dknet_lan_context* context=nullptr;
    uint64_t sockets[3]{};
    dknet_endpoint peer{};
    unsigned sent[3]{};
  } s;
  dknet_lan_config c{}; c.struct_size=sizeof(c); c.version=DKNET_LAN_VERSION;
  c.maximum_sockets=3; c.maximum_memberships=1;
  c.maximum_pending_datagrams=4; c.maximum_pending_bytes=64; c.user=&s;
  c.on_event=[](void* user,const dknet_lan_event* e) {
    auto& s=*static_cast<State*>(user);
    CHECK(e->type!=DKNET_LAN_TIMER && e->type!=DKNET_LAN_ERROR);
    if(e->type!=DKNET_LAN_SENT) return;
    CHECK(e->status==DKNET_OK);
    unsigned socket=0;
    while(socket<3 && e->socket!=s.sockets[socket]) ++socket;
    CHECK(socket<3); ++s.sent[socket];
    if(!socket) {
      // This legal callback keeps the earliest socket busy indefinitely. Every
      // other accepted send must complete while it is still being replenished.
      const uint8_t byte=42;
      CHECK(dknet_udp_send(s.context,s.sockets[0],&s.peer,&byte,1,0)==DKNET_OK);
    }
    if(s.sent[1] && s.sent[2]) {
      CHECK(s.sent[0]);
      CHECK(dknet_lan_shutdown(s.context)==DKNET_OK);
    }
  };
  CHECK(dknet_lan_create(&c,&s.context)==DKNET_OK);
  dknet_udp_config udp{}; udp.struct_size=sizeof(udp); udp.version=DKNET_LAN_VERSION;
  udp.maximum_receive_bytes=64; udp.multicast_ttl=1; udp.multicast_loop=1;
  std::strcpy(udp.bind.address,"127.0.0.1");
  for(auto& socket:s.sockets) CHECK(dknet_udp_open(s.context,&udp,&socket)==DKNET_OK);
  CHECK(dknet_udp_local_endpoint(s.context,s.sockets[0],&s.peer)==DKNET_OK);
  CHECK(dknet_udp_receive_start(s.context,s.sockets[0])==DKNET_OK);
  const uint8_t byte=42;
  for(auto socket:s.sockets) CHECK(dknet_udp_send(s.context,socket,&s.peer,&byte,1,0)==DKNET_OK);
  CHECK(dknet_lan_timer_start(s.context,0,200,0)==DKNET_OK);
  CHECK(dknet_lan_run(s.context)==DKNET_OK);
  CHECK(s.sent[1]==1 && s.sent[2]==1);
  CHECK(dknet_lan_destroy(s.context)==DKNET_OK);
}

int main() {
  completion_and_close(); callback_refill_fairness();
  std::fprintf(stderr,"Apple copied sends, capacity, callback/pending close/rebind and callback-refill fairness passed\n");
}
