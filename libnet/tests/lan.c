#include "libnet/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
struct state {
  dknet_lan_context* context;
  uint64_t socket;
  dknet_endpoint local, group;
  unsigned received,sent,closed,rebound,writable,interfaces,dropped,phase;
};
static int32_t literal_endpoint(const char* address,uint16_t port,dknet_endpoint* out) {
  if(strlen(address)>=sizeof(out->address)) return DKNET_INVALID;
  memset(out,0,sizeof(*out)); memcpy(out->address,address,strlen(address)+1); out->port=port; return DKNET_OK;
}
static const char* group="239.255.42.99";
static void send(struct state* s,const dknet_endpoint* to,const char* bytes,size_t size,uint64_t token) {
  CHECK(dknet_udp_send(s->context,s->socket,to,(const uint8_t*)bytes,size,token)==DKNET_OK);
}
static void progress(struct state* s) {
  if(s->phase==0 && s->received==1 && s->sent==1) {
    s->phase=1;
    CHECK(dknet_udp_rebind(s->context,s->socket)==DKNET_OK);
    CHECK(dknet_udp_rebind(s->context,s->socket)==DKNET_BUSY);
    CHECK(dknet_udp_send(s->context,s->socket,&s->local,(const uint8_t*)"ping",4,2)==DKNET_STATE);
  } else if(s->phase==2 && s->received==2 && s->sent==2) {
    s->phase=3;
    send(s,&s->local,"oversize",8,3);
  } else if(s->phase==3 && s->dropped==1 && s->sent==3) {
    s->phase=4;
    CHECK(dknet_udp_receive_stop(s->context,s->socket)==DKNET_OK);
    send(s,&s->local,"ping",4,4);
    CHECK(dknet_lan_timer_start(s->context,1,30,0)==DKNET_OK);
  } else if(s->phase==5 && s->received==3 && s->sent==4) {
    s->phase=6;
    CHECK(dknet_udp_membership(s->context,s->socket,group,"127.0.0.1",0)==DKNET_OK);
    CHECK(dknet_udp_membership(s->context,s->socket,group,"127.0.0.1",0)==DKNET_NOT_FOUND);
    send(s,&s->local,"ping",4,5);
    CHECK(dknet_udp_close(s->context,s->socket)==DKNET_OK);
    CHECK(dknet_udp_close(s->context,s->socket)==DKNET_OK);
    CHECK(dknet_udp_send(s->context,s->socket,&s->local,(const uint8_t*)"ping",4,6)==DKNET_STATE);
  }
}
static void DKNET_CALL event(void* user,const dknet_lan_event* e) {
  struct state* s=(struct state*)user;
  CHECK(e->struct_size==sizeof(*e));
  CHECK(dknet_lan_destroy(s->context)==DKNET_STATE);
  CHECK(dknet_lan_run(s->context)==DKNET_STATE);
  if(e->type==DKNET_LAN_DATAGRAM) {
    CHECK(e->socket==s->socket && e->size==4 && !memcmp(e->data,"ping",4));
    CHECK(s->phase<6); ++s->received;
  } else if(e->type==DKNET_LAN_SENT) {
    CHECK(e->socket==s->socket && e->request==s->sent+1);
    if(s->phase<6) CHECK(e->status==DKNET_OK);
    ++s->sent;
  } else if(e->type==DKNET_LAN_REBOUND) {
    dknet_endpoint bound;
    CHECK(e->status==DKNET_OK && s->phase==1);
    CHECK(dknet_udp_local_endpoint(s->context,s->socket,&bound)==DKNET_OK && bound.port==s->local.port);
    ++s->rebound; s->phase=2;
    send(s,&s->group,"ping",4,2); /* Restored membership and multicast output interface. */
  } else if(e->type==DKNET_LAN_WRITABLE) ++s->writable;
  else if(e->type==DKNET_LAN_INTERFACES) {
    size_t count=0; uint64_t generation=0;
    CHECK(dknet_lan_interfaces(s->context,NULL,0,&count,&generation)==DKNET_OK);
    CHECK(count>0 && generation==e->generation && generation==1);
    ++s->interfaces;
    CHECK(dknet_lan_refresh_interfaces(s->context)==DKNET_OK); /* unchanged: no extra event */
  } else if(e->type==DKNET_LAN_ERROR) {
    CHECK(e->status==DKNET_BUFFER_TOO_SMALL && e->socket==s->socket);
    ++s->dropped; CHECK(s->received==2);
  } else if(e->type==DKNET_LAN_TIMER) {
    CHECK(e->request==1 && s->phase==4 && s->received==2);
    s->phase=5; CHECK(dknet_udp_receive_start(s->context,s->socket)==DKNET_OK);
  } else if(e->type==DKNET_LAN_CLOSED) {
    dknet_udp_stats stats={0}; stats.struct_size=sizeof(stats);
    CHECK(e->socket==s->socket && s->phase==6 && s->sent==5);
    CHECK(dknet_udp_statistics(s->context,s->socket,&stats)==DKNET_OK);
    CHECK(!stats.pending_bytes && !stats.pending_datagrams && stats.received_datagrams==3 && stats.dropped_datagrams==1);
    ++s->closed;
    CHECK(dknet_lan_shutdown(s->context)==DKNET_OK);
  } else CHECK(0);
  progress(s);
}
int main(void) {
  size_t count=0; CHECK(dknet_interface_list(NULL,0,&count)==DKNET_OK && count>0);
  dknet_interface* rows=(dknet_interface*)calloc(count,sizeof(*rows)); CHECK(rows);
  size_t found=count; CHECK(dknet_interface_list(rows,count,&found)==DKNET_OK && found==count);
  for(size_t i=0;i<count;++i) CHECK(rows[i].struct_size==sizeof(*rows) && rows[i].index && (rows[i].family==4 || rows[i].family==6) && rows[i].name[0] && rows[i].address[0]);
  dknet_interface sentinel; memset(&sentinel,0x5a,sizeof(sentinel));
  CHECK(dknet_interface_list(&sentinel,0,&found)==DKNET_BUFFER_TOO_SMALL && ((unsigned char*)&sentinel)[0]==0x5a);
  free(rows);
  struct state s={0};
  dknet_lan_config config={0}; config.struct_size=sizeof(config); config.version=1;
  config.maximum_sockets=4; config.maximum_memberships=1; config.maximum_pending_datagrams=1;
  config.maximum_pending_bytes=8; config.on_event=event; config.user=&s;
  CHECK(dknet_lan_version()==1 && dknet_lan_create(&config,&s.context)==DKNET_OK);
  dknet_udp_config udp={0}; udp.struct_size=sizeof(udp); udp.version=1;
  udp.flags=DKNET_UDP_REUSE_ADDRESS|DKNET_UDP_BROADCAST; udp.maximum_receive_bytes=4;
  udp.multicast_ttl=1; udp.multicast_loop=1;
  CHECK(literal_endpoint("0.0.0.0",0,&udp.bind)==DKNET_OK);
  CHECK(dknet_udp_open(s.context,&udp,&s.socket)==DKNET_OK);
  CHECK(dknet_udp_local_endpoint(s.context,s.socket,&s.local)==DKNET_OK);
  CHECK(literal_endpoint("127.0.0.1",s.local.port,&s.local)==DKNET_OK);
  CHECK(literal_endpoint(group,s.local.port,&s.group)==DKNET_OK);
  CHECK(dknet_udp_multicast_interface(s.context,s.socket,"127.0.0.1")==DKNET_OK);
  CHECK(dknet_udp_membership(s.context,s.socket,group,"127.0.0.1",1)==DKNET_OK);
  CHECK(dknet_udp_membership(s.context,s.socket,group,"127.0.0.1",1)==DKNET_OK);
  CHECK(dknet_udp_membership(s.context,s.socket,"239.255.42.98","127.0.0.1",1)==DKNET_BUSY);
  CHECK(dknet_udp_membership(s.context,s.socket,"127.0.0.1",NULL,1)==DKNET_INVALID);
  CHECK(dknet_udp_membership(s.context,s.socket,group,"::1",1)==DKNET_INVALID);
  CHECK(dknet_udp_receive_start(s.context,s.socket)==DKNET_OK);
  CHECK(dknet_lan_watch_interfaces(s.context,250)==DKNET_OK);
  CHECK(dknet_lan_watch_interfaces(s.context,1)==DKNET_INVALID);
  CHECK(dknet_lan_timer_start(s.context,0,3000,0)==DKNET_OK); /* timeout is fatal */
  uint8_t bytes[]={'p','i','n','g'};
  CHECK(dknet_udp_send(s.context,s.socket,&s.local,bytes,4,1)==DKNET_OK);
  memset(bytes,0,sizeof(bytes));
  CHECK(dknet_udp_send(s.context,s.socket,&s.local,(const uint8_t*)"ping",4,9)==DKNET_BUSY);
  CHECK(dknet_lan_run(s.context)==DKNET_OK);
  CHECK(s.closed==1 && s.rebound==1 && s.writable==1 && s.interfaces==1 && s.dropped==1);
  CHECK(dknet_lan_wake(s.context,0)==DKNET_STATE);
  CHECK(dknet_lan_destroy(s.context)==DKNET_OK);
  puts("LAN C ABI: bounded copy, IPv4 multicast/rebind, truncation, pause, snapshots and close passed");
  return 0;
}
