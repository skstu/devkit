#include "libnet/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
struct state { dknet_context *context; dknet_endpoint peer; int received, rebound, writable; };
static void DKNET_CALL event(void *user,const dknet_event *e) {
  struct state *s=(struct state *)user;
  CHECK(e->struct_size==sizeof(*e));
  CHECK(dknet_destroy(s->context)==DKNET_STATE);
  if(e->type==DKNET_DATAGRAM) {
    CHECK(e->size==4 && memcmp(e->data,"ping",4)==0);
    if(++s->received==1) CHECK(dknet_rebind(s->context)==DKNET_OK);
    else CHECK(dknet_shutdown(s->context)==DKNET_OK);
  } else if(e->type==DKNET_REBOUND) {
    dknet_endpoint local;
    CHECK(e->status==DKNET_OK); s->rebound++;
    CHECK(dknet_local_endpoint(s->context,4,&local)==DKNET_OK && local.port==s->peer.port);
    CHECK(dknet_send_datagram(s->context,&s->peer,(const uint8_t *)"ping",4)==DKNET_OK);
  } else if(e->type==DKNET_WRITABLE) s->writable++;
  else if(e->type==DKNET_TIMER || e->type==DKNET_ERROR) CHECK(0);
}
int main(void) {
  struct state s={0};
  dknet_config config={0};
  config.struct_size=sizeof(config); config.abi_version=DKNET_ABI_VERSION;
  config.maximum_connections=8; config.maximum_pending_datagrams=1;
  config.maximum_pending_bytes=4; config.quic_tick_ms=10; config.alpn="independent-example/1";
  config.on_event=event; config.user=&s;
  CHECK(dknet_abi_version()==1 && strlen(dknet_version())>0);
  config.abi_version=2;
  CHECK(dknet_create(&config,&s.context)==DKNET_INVALID && !s.context);
  config.abi_version=1;
  CHECK(dknet_create(&config,&s.context)==DKNET_OK);
  CHECK(dknet_local_endpoint(s.context,4,&s.peer)==DKNET_OK);
  CHECK(dknet_endpoint_parse("127.0.0.1",s.peer.port,&s.peer)==DKNET_OK);
  CHECK(dknet_timer_start(s.context,0,3000,0)==DKNET_OK);
  uint8_t copied[]={'p','i','n','g'};
  CHECK(dknet_send_datagram(s.context,&s.peer,copied,4)==DKNET_OK);
  memset(copied,0,sizeof(copied));
  CHECK(dknet_send_datagram(s.context,&s.peer,(const uint8_t *)"ping",4)==DKNET_BUSY);
  dknet_context *conflict=NULL;
  config.port=s.peer.port;
  CHECK(dknet_create(&config,&conflict)==DKNET_IO && conflict==NULL);
  CHECK(dknet_run(s.context)==DKNET_OK);
  CHECK(s.received==2 && s.rebound==1 && s.writable==1);
  CHECK(dknet_wake(s.context,0)==DKNET_STATE);
  CHECK(dknet_destroy(s.context)==DKNET_OK);
  dknet_probe_result probe={0}; probe.struct_size=sizeof(probe);
  CHECK(dknet_quic_probe("example-probe/2",(const uint8_t *)"hello",5,3000,&probe)==DKNET_OK);
  CHECK(probe.handshake_completed && probe.stream_completed && probe.udp_socket_io && !probe.early_data);
  CHECK(strcmp(probe.alpn,"example-probe/2")==0 && probe.error[0]==0);
  puts("C ABI: UDP copy/queue limit, rebind, shutdown passed");
  return 0;
}
