#include <libble/ble.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static dkble_context *context;
static int wakes, events;
static void wake(void *user) { CHECK(user == &wakes); ++wakes; CHECK(dkble_stop(context) == DKBLE_BUSY); }
static void event(void *user, const dkble_event *value) {
  (void)user; ++events;
  CHECK(value->type == DKBLE_EVENT_STATE && strcmp(value->detail, "off") == 0);
  CHECK(dkble_destroy(context) == DKBLE_BUSY);
  CHECK(dkble_dispatch(context, 1, event, NULL) == DKBLE_BUSY);
}
static void *wrong_thread(void *unused) { (void)unused; CHECK(dkble_stop(context) == DKBLE_THREAD); CHECK(dkble_destroy(context) == DKBLE_THREAD); return NULL; }
int main(void) {
  dkble_config c = {0}; c.struct_size = sizeof(c); c.abi_version = 1;
  c.service_uuid = "00000000-0000-0000-0000-000000000001";
  c.receive_uuid = "00000000-0000-0000-0000-000000000002";
  c.notify_uuid = "00000000-0000-0000-0000-000000000003";
  c.wake = wake; c.user = &wakes;
  CHECK(dkble_abi_version() == 1 && strlen(dkble_version()) > 0);
  c.flags = 2; CHECK(dkble_create(&c, &context) == DKBLE_INVALID && !context); c.flags = 0;
  CHECK(dkble_create(&c, &context) == DKBLE_OK && context);
  CHECK(wakes == 0); /* Creation does not access the radio or ask permission. */
  CHECK(dkble_start(context, 0) == DKBLE_INVALID);
  CHECK(dkble_connect(context, c.service_uuid) == DKBLE_STATE);
  CHECK(dkble_send(context, c.service_uuid, (const uint8_t *)"x", 1, 7) == DKBLE_STATE);
  pthread_t thread; CHECK(pthread_create(&thread, NULL, wrong_thread, NULL) == 0); pthread_join(thread, NULL);
  CHECK(dkble_stop(context) == DKBLE_OK && wakes == 1);
  CHECK(dkble_dispatch(context, 256, event, NULL) == 1 && events == 1);
  CHECK(dkble_destroy(context) == DKBLE_OK); context = NULL;
  puts("libble C ABI lifetime/thread/reentrancy passed (radio untouched)");
  return 0;
}
