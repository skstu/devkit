#include <libnet/net.h>
#include <stdio.h>
#include <string.h>
struct example { dknet_lan_context *context; uint64_t socket; int received; };
static void DKNET_CALL event(void *user,const dknet_lan_event *e) {
  struct example *s=(struct example *)user;
  if(e->type==DKNET_LAN_DATAGRAM) {
    s->received=e->size==3 && memcmp(e->data,"sdk",3)==0;
    dknet_udp_close(s->context,s->socket);
  } else if(e->type==DKNET_LAN_CLOSED || e->type==DKNET_LAN_TIMER || e->type==DKNET_LAN_ERROR)
    dknet_lan_shutdown(s->context);
}
int main(void) {
  struct example s={0};
  dknet_lan_config config={0}; config.struct_size=sizeof(config); config.version=DKNET_LAN_VERSION;
  config.maximum_sockets=1; config.maximum_memberships=1;
  config.maximum_pending_datagrams=2; config.maximum_pending_bytes=64;
  config.on_event=event; config.user=&s;
  if(dknet_lan_version()!=1 || dknet_lan_create(&config,&s.context)) return 1;
  dknet_udp_config udp={0}; udp.struct_size=sizeof(udp); udp.version=1;
  udp.maximum_receive_bytes=64; udp.multicast_ttl=1; udp.multicast_loop=1;
  memcpy(udp.bind.address,"127.0.0.1",10);
  dknet_endpoint local;
  int status=dknet_udp_open(s.context,&udp,&s.socket);
  if(!status) status=dknet_udp_local_endpoint(s.context,s.socket,&local);
  if(!status) status=dknet_udp_receive_start(s.context,s.socket);
  if(!status) status=dknet_lan_timer_start(s.context,0,3000,0);
  if(!status) status=dknet_udp_send(s.context,s.socket,&local,(const uint8_t *)"sdk",3,1);
  if(!status) status=dknet_lan_run(s.context);
  if(dknet_lan_destroy(s.context)) return 1;
  if(status || !s.received) return 1;
  puts("Pure C LAN SDK consumer: copied datagram and confirmed close passed");
  return 0;
}
