#include <libice/ice.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void) {
  dkice_config c={0}; c.struct_size=sizeof(c); c.abi_version=1; c.address_family=4; c.bind_address="127.0.0.1";
  dkice_context *agent=NULL;
  CHECK(dkice_abi_version()==1 && strlen(dkice_version())>0);
  c.flags=DKICE_RELAY_ONLY; CHECK(dkice_create(&c,&agent)==DKICE_INVALID && !agent); c.flags=0;
  c.stun_address="example.invalid"; c.stun_port=3478; CHECK(dkice_create(&c,&agent)==DKICE_INVALID); c.stun_address=NULL;
  c.port_begin=50000; CHECK(dkice_create(&c,&agent)==DKICE_INVALID); c.port_begin=0;
  CHECK(dkice_create(&c,&agent)==DKICE_OK);
  CHECK(dkice_send(agent,(const uint8_t *)"a",1)==DKICE_STATE);
  size_t size=0; CHECK(dkice_local_description(agent,NULL,0,&size)==DKICE_BUFFER && size>0);
  char tiny[1]={'!'}; CHECK(dkice_local_description(agent,tiny,1,&size)==DKICE_BUFFER && tiny[0]=='!');
  CHECK(dkice_remote_description(agent,"a\0b",3)==DKICE_INVALID);
  CHECK(dkice_stop(agent)==DKICE_OK);
  CHECK(dkice_gather(agent)==DKICE_STATE && dkice_send(agent,(const uint8_t *)"a",1)==DKICE_STATE);
  CHECK(dkice_destroy(agent)==DKICE_OK);
  puts("libice C ABI validation/buffer/lifetime passed"); return 0;
}
