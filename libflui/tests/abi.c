#include <libflui/desktop.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static flui_window window;
static int failed, complete, ticks, ready;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                     \
      failed = 1;                                                              \
    }                                                                          \
  } while (0)
static flui_string span(const char *s) {
  flui_string out = {s, strlen(s)};
  return out;
}
static void *wrong_thread(void *unused) {
  (void)unused;
  CHECK(flui_window_show(window) == FLUI_WRONG_THREAD);
  return NULL;
}
static void event(const flui_event *e, void *user) {
  (void)user;
  if (e->kind == FLUI_EVENT_READY) {
    ++ready;
    CHECK(flui_window_destroy(window) == FLUI_BUSY);
    CHECK(flui_app_run() == FLUI_BUSY);
    CHECK(flui_window_load_xml(
              window, span("<Window><Label text='{result}'/></Window>"), 1) ==
          FLUI_OK);
    CHECK(flui_window_load_xml(window, span("<Window><Unknown/></Window>"),
                               2) == FLUI_OK);
    CHECK(flui_window_set_state(window, span("{\"result\":\"C ABI 中文\"}"),
                                3) == FLUI_OK);
    CHECK(flui_window_set_state(window, span("[]"), 4) == FLUI_OK);
    for (int i = 0; i < 12; ++i)
      CHECK(flui_window_set_state(window, span("{\"result\":\"queued\"}"),
                                  10 + i) == FLUI_OK);
    CHECK(flui_window_set_state(window, span("{}"), 99) == FLUI_BUSY);
    flui_string oversized = {"", 1024 * 1024 + 1};
    CHECK(flui_window_load_xml(window, oversized, 100) == FLUI_LIMIT_EXCEEDED);
    flui_string invalid = {"\xff", 1};
    CHECK(flui_window_load_xml(window, invalid, 101) == FLUI_INVALID_ARGUMENT);
  } else if (e->kind == FLUI_EVENT_COMPLETE) {
    CHECK(e->status == ((e->request_id == 2 || e->request_id == 4)
                            ? FLUI_DOCUMENT_ERROR
                            : FLUI_OK));
    if (++complete == 16)
      flui_app_quit();
  } else if (e->kind == FLUI_EVENT_ERROR) {
    failed = 1;
    flui_app_quit();
  } else if (e->kind == FLUI_EVENT_TICK && ++ticks > 100) {
    fprintf(stderr, "engine timeout\n");
    failed = 1;
    flui_app_quit();
  }
}
static int xml_events;
static int32_t xml(uint32_t kind, flui_string name, flui_string value,
                   void *user) {
  (void)kind;
  (void)name;
  (void)value;
  (void)user;
  ++xml_events;
  return 0;
}
static void desktop_services(void) {
  CHECK(
      flui_xml_visit(span("<Window><Label text='中文 &amp; exact'/></Window>"),
                     xml, NULL) == FLUI_OK);
  CHECK(xml_events == 5);
  CHECK(flui_xml_visit(span("<!DOCTYPE x SYSTEM 'file:///invalid'><Window/>"),
                       xml, NULL) == FLUI_DOCUMENT_ERROR);
  CHECK(flui_xml_visit(span("<Window>"), xml, NULL) == FLUI_DOCUMENT_ERROR);
  char path[] = "/tmp/libflui-abi-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd >= 0)
    close(fd);
  const size_t size = 2 * 1024 * 1024;
  char *content = malloc(size);
  CHECK(content != NULL);
  if (!content)
    return;
  memset(content, 'x', size);
  content[0] = 0;
  content[size - 1] = 'z';
  CHECK(flui_write_file_atomic(span(path), (flui_string){content, size}) ==
        FLUI_OK);
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL);
  if (f) {
    CHECK(fread(content, 1, size, f) == size);
    CHECK(content[0] == 0 && content[size - 1] == 'z');
    CHECK(fgetc(f) == EOF);
    fclose(f);
  }
  CHECK(flui_write_file_atomic(span(path),
                               (flui_string){"", 64u * 1024u * 1024u + 1}) ==
        FLUI_LIMIT_EXCEEDED);
  free(content);
  unlink(path);
}
int main(void) {
  desktop_services();
  CHECK(flui_abi_version() == FLUI_ABI_VERSION);
  flui_window_options o = {
      sizeof(o), FLUI_ABI_VERSION, 400, 200, {"libflui ABI fixture", 19}, event,
      NULL};
  o.abi_version = 2;
  CHECK(flui_window_create(&o, &window) == FLUI_ABI_MISMATCH);
  o.abi_version = 1;
  for (int cycle = 0; cycle < 2; ++cycle) {
    complete = ready = ticks = 0;
    CHECK(flui_window_create(&o, &window) == FLUI_OK);
    if (!window)
      return 1;
    CHECK(flui_window_load_xml(window, span("<Window/>"), 0) == FLUI_NOT_READY);
    pthread_t thread;
    pthread_create(&thread, NULL, wrong_thread, NULL);
    pthread_join(thread, NULL);
    CHECK(flui_window_set_tick(window, 100) == FLUI_OK);
    CHECK(flui_app_run() == FLUI_OK);
    CHECK(ready == 1 && complete == 16);
    CHECK(flui_window_destroy(window) == FLUI_OK);
    CHECK(flui_window_show(window) == FLUI_INVALID_HANDLE);
    CHECK(flui_window_destroy(window) == FLUI_INVALID_HANDLE);
  }
  if (!failed)
    puts("PASS pure C ABI: real AOT engine, UTF-8, XML/state errors, queue "
         "bounds, thread checks, restart/shutdown");
  return failed;
}
