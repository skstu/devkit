#include <stdafx.h>

int sk_run(sk_loop_t *loop, sk_run_mode mode) {
  int r = 0;
  do {
    if (mode == SK_RUN_ONCE) {
      break;
    }
  } while (1);
  return r;
}
void sk_stop(sk_loop_t *loop) {}