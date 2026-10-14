#include "libnet/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"TCP check %d: %s\n",__LINE__,#x);abort();}}while(0)
struct state {dknet_context* c;uint64_t out,in;char received[64];size_t size;unsigned sent,closed,callbacks;int paused, cancel_write;};
static void DKNET_CALL event(void* user,const dknet_event* e) {
  struct state* s=user;
  if(e->type==DKNET_TIMER) {
    CHECK(e->slot==1 && s->paused);
    CHECK(dknet_tcp_pause(s->c,s->in,0)==0);s->paused=0;
  }
}
static void DKNET_CALL tcp(void* user,const dknet_tcp_event* e) {
  struct state* s=user;++s->callbacks;
  CHECK(e->struct_size==sizeof(*e));CHECK(dknet_destroy(s->c)==DKNET_STATE);
  if(e->type==DKNET_TCP_ACCEPTED) {
    s->in=e->connection;s->paused=1;CHECK(dknet_tcp_pause(s->c,s->in,1)==0);
    CHECK(dknet_timer_start(s->c,1,20,0)==0);
  } else if(e->type==DKNET_TCP_CONNECTED) {
    if(s->cancel_write) {
      const size_t size=4*1024*1024;unsigned char* data=malloc(size);CHECK(data);memset(data,0x51,size);
      CHECK(dknet_tcp_send(s->c,s->out,data,size)==0);free(data);
      CHECK(dknet_tcp_send(s->c,s->out,(const unsigned char*)"x",1)==DKNET_BUSY);
      CHECK(dknet_shutdown(s->c)==0);return;
    }
    unsigned char oversized[65]={0};CHECK(dknet_tcp_send(s->c,s->out,oversized,sizeof(oversized))==DKNET_INVALID);
    unsigned char first[]="first";
    CHECK(e->connection==s->out);
    CHECK(dknet_tcp_send(s->c,s->out,first,5)==0);memset(first,0,5);
    CHECK(dknet_tcp_send(s->c,s->out,(const unsigned char*)"second",6)==0);
    CHECK(dknet_tcp_send(s->c,s->out,(const unsigned char*)"must-not-appear",15)==DKNET_BUSY);
  } else if(e->type==DKNET_TCP_SENT) {
    CHECK(e->status==0);++s->sent;
  } else if(e->type==DKNET_TCP_DATA) {
    CHECK(e->connection==s->in && s->size+e->size<=sizeof(s->received));
    memcpy(s->received+s->size,e->data,e->size);s->size+=e->size;
    if(s->size==11) {
      CHECK(!memcmp(s->received,"firstsecond",11));
      CHECK(dknet_tcp_close(s->c,s->out)==0);CHECK(dknet_tcp_close(s->c,s->in)==0);
    }
  } else if(e->type==DKNET_TCP_CLOSED) {
    if(++s->closed==2)CHECK(dknet_shutdown(s->c)==0);
  }
}
int main(void) {
  struct state s={0};dknet_config cfg={0};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
  cfg.maximum_connections=2;cfg.maximum_pending_datagrams=8;cfg.maximum_pending_bytes=65556;cfg.quic_tick_ms=10;
  cfg.alpn="tcp-test/1";cfg.on_event=event;cfg.user=&s;
  CHECK(dknet_create(&cfg,&s.c)==0);
  dknet_tcp_config tc={sizeof(tc),DKNET_TCP_VERSION,2,2,64,tcp,&s};
  CHECK(dknet_tcp_configure(s.c,&tc)==0);CHECK(dknet_tcp_configure(s.c,&tc)==DKNET_STATE);
  dknet_endpoint local;CHECK(dknet_local_endpoint(s.c,4,&local)==0);
  CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&local)==0);
  CHECK(dknet_tcp_listen(s.c,&local)==0);CHECK(dknet_tcp_connect(s.c,&local,&s.out)==0);
  CHECK(dknet_timer_start(s.c,0,2000,0)==0);CHECK(dknet_run(s.c)==0);
  CHECK(s.sent==2 && s.closed==2 && s.size==11);
  unsigned count=s.callbacks;CHECK(dknet_destroy(s.c)==0);CHECK(count==s.callbacks);
  // Cancel before run: libuv must drain connect/write/close requests without a callback.
  memset(&s,0,sizeof(s));CHECK(dknet_create(&cfg,&s.c)==0);CHECK(dknet_tcp_configure(s.c,&tc)==0);
  CHECK(dknet_local_endpoint(s.c,4,&local)==0);CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&local)==0);
  CHECK(dknet_tcp_listen(s.c,&local)==0);CHECK(dknet_tcp_connect(s.c,&local,&s.out)==0);
  CHECK(dknet_shutdown(s.c)==0);CHECK(dknet_destroy(s.c)==0);CHECK(!s.callbacks);
  // A paused peer plus a large uv_write exercises partial/pending writes and
  // cancellation; libuv owns offsets, and destroy must drain all write credits.
  memset(&s,0,sizeof(s));s.cancel_write=1;tc.maximum_pending_bytes=4*1024*1024;
  CHECK(dknet_create(&cfg,&s.c)==0);CHECK(dknet_tcp_configure(s.c,&tc)==0);
  CHECK(dknet_local_endpoint(s.c,4,&local)==0);CHECK(dknet_endpoint_parse("127.0.0.1",local.port,&local)==0);
  CHECK(dknet_tcp_listen(s.c,&local)==0);CHECK(dknet_tcp_connect(s.c,&local,&s.out)==0);
  CHECK(dknet_run(s.c)==0);count=s.callbacks;CHECK(dknet_destroy(s.c)==0);CHECK(count==s.callbacks && !s.sent);
  puts("TCP C ABI: ordered stream/copy, bounded busy, pause, cancel/shutdown drain passed");return 0;
}
