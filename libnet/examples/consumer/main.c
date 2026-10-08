#include <libnet/net.h>
#include <stdio.h>
int main(void) {
  if(dknet_abi_version()!=DKNET_ABI_VERSION) return 1;
  printf("libnet %s / ABI %u\n",dknet_version(),dknet_abi_version());
  return 0;
}
