#include <stdafx.h>

sk_tlock_t *sk_tlock_create(void) {
  sk_tlock_t *r = NULL;
  do {
    r = sk__malloc(sizeof(*r));
    if (!r)
      break;
    r->t_handle = sk__malloc(sizeof(pthread_mutex_t));
    if (!r->t_handle)
      break;
    if (0 != pthread_mutex_init((pthread_mutex_t *)r->t_handle, NULL)) {
      sk__free_s((void **)&r->t_handle);
      break;
    }
  } while (0);
  return r;
}
int sk_tlock_destroy(sk_tlock_t **ph) {
  int r = SK_NO;
  if (*ph) {
    if (!pthread_mutex_destroy((pthread_mutex_t *)(*ph)->t_handle)) {
      r = SK_OK;
    }
    sk__free_s((void **)ph);
  }
  return r;
}
int sk_tlock_lock(sk_tlock_t *h) {
  int r = SK_NO;
  if (h) {
    if (!pthread_mutex_lock((pthread_mutex_t *)h->t_handle)) {
      r = SK_OK;
    }
  }
  return r;
}
int sk_tlock_trylock(sk_tlock_t *h) {
  int r = SK_NO;
  if (h) {
    if (!pthread_mutex_trylock((pthread_mutex_t *)h->t_handle)) {
      r = SK_OK;
    }
  }
  return r;
}
int sk_tlock_unlock(sk_tlock_t *h) {
  int r = SK_NO;
  if (h) {
    if (!pthread_mutex_unlock((pthread_mutex_t *)h->t_handle)) {
      r = SK_OK;
    }
  }
  return r;
}
