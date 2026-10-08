#include <libice/ice.h>
#include <stdio.h>
int main(void) {
  dkice_config config={0};config.struct_size=sizeof(config);config.abi_version=DKICE_ABI_VERSION;
  config.bind_address="127.0.0.1";config.address_family=4;
  dkice_context *context=NULL;
  if(dkice_create(&config,&context)!=DKICE_OK)return 1;
  printf("libice %s ABI %u (%s)\n",dkice_version(),dkice_abi_version(),dkice_backend_version());
  return dkice_destroy(context)!=DKICE_OK;
}
