#include <stdafx.h>

sk_tlock_t *sk_tlock_create(void) {
  sk_tlock_t *r = NULL;
  int ret = SK_NO;
  do {
    r = (sk_tlock_t *)sk__malloc(sizeof(*r));
    if (!r)
      break;
    r->t_handle = sk__malloc(sizeof(CRITICAL_SECTION));
    if (!r->t_handle)
      break;
    InitializeCriticalSection((CRITICAL_SECTION *)r->t_handle);
    ret = SK_OK;
  } while (0);
  if (ret != SK_OK && r)
    sk_tlock_destroy(&r);
  return r;
}
int sk_tlock_destroy(sk_tlock_t **ph) {
  int r = SK_NO;
  do {
    if (!*ph)
      break;
    DeleteCriticalSection((CRITICAL_SECTION *)(*ph)->t_handle);
    sk__free_s((void **)ph);
    r = SK_OK;
  } while (0);
  return r;
}
int sk_tlock_lock(sk_tlock_t *h) {
  int r = SK_NO;
  do {
    if (!h)
      break;
    EnterCriticalSection((CRITICAL_SECTION *)h->t_handle);
    r = SK_OK;
  } while (0);
  return r;
}
int sk_tlock_trylock(sk_tlock_t *h) {
  int r = SK_NO;
  do {
    if (!h)
      break;
    if (FALSE == TryEnterCriticalSection((CRITICAL_SECTION *)h->t_handle))
      break;
    r = SK_OK;
  } while (0);
  return r;
}
int sk_tlock_unlock(sk_tlock_t *h) {
  int r = SK_NO;
  do {
    if (!h)
      break;
    LeaveCriticalSection((CRITICAL_SECTION *)h->t_handle);
    r = SK_OK;
  } while (0);
  return r;
}
