#include <libble/ble.h>
#include <stdio.h>
int main(void) {
  dkble_config c = {0}; c.struct_size = sizeof(c); c.abi_version = DKBLE_ABI_VERSION;
  c.service_uuid = "00000000-0000-0000-0000-000000000001";
  c.receive_uuid = "00000000-0000-0000-0000-000000000002";
  c.notify_uuid = "00000000-0000-0000-0000-000000000003";
  dkble_context *context = NULL;
  if (dkble_create(&c, &context) != DKBLE_OK) return 1;
  printf("libble %s ABI %u; created without radio access\n", dkble_version(), dkble_abi_version());
  return dkble_destroy(context) != DKBLE_OK;
}
