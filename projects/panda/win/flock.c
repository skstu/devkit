#include <stdafx.h>

sk_flock_t *sk_fwlock_create(const char *fpath) {
  sk_flock_t *r = NULL;
  int err = SK_NO;
  do {
    if (!fpath)
      break;
    r = (sk_flock_t *)sk__malloc(sizeof(*r));
    if (!r)
      break;
    r->flk_file = sk_file_open(fpath, "w");
    if (!r->flk_file)
      break;
    r->flk_status = SK_F_STATUS_WRLCK;
    err = SK_OK;
  } while (0);
  if (err && r)
    sk_fwlock_destroy(&r);
  return r;
}
int sk_fwlock_destroy(sk_flock_t **ph) {
  int r = SK_NO;
  do {
    if (!*ph) {
      r = SK_OK;
      break;
    }
    if ((*ph)->flk_file)
      r = sk_file_close(&(*ph)->flk_file);
    sk__free_s((void **)ph);
  } while (0);
  return r;
}
int fd_ = 0;
int sk_fwlock_lock(sk_flock_t *f, size_t offset, size_t len) {
  int r = SK_NO;
  do {
    if (!f)
      break;
    if (!f->flk_file)
      break;
    HANDLE fh = *((HANDLE *)f->flk_file->f_handle);
    if (!fh)
      break;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)offset;
    do {
      if (TRUE ==
          LockFileEx(fh, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                     (DWORD)len, 0, &overlapped)) {
        break;
      }
      DWORD dwLastError = GetLastError();
      if (ERROR_LOCK_VIOLATION != dwLastError) {
        break;
      }
      printf("File locked.\n");
      Sleep(10);
    } while (1);
    r = SK_OK;
  } while (0);
  return r;
}
int sk_fwlock_trylock(sk_flock_t *f, size_t offset, size_t len) {
  int r = SK_NO;
  do {
    if (!f)
      break;
    if (!f->flk_file)
      break;
    HANDLE fh = *((HANDLE *)f->flk_file->f_handle);
    if (!fh)
      break;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)offset;
    if (FALSE == LockFileEx(fh,
                            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                            0, (DWORD)len, 0, &overlapped)) {
      DWORD dwLastError = GetLastError();
      if (ERROR_LOCK_VIOLATION != dwLastError) {
        //!@ Error
      }
      break;
    }
    r = SK_OK;
  } while (0);
  return r;
}
int sk_fwlock_unlock(sk_flock_t *f, size_t offset, size_t len) {
  int r = SK_NO;
  do {
    if (!f)
      break;
    if (!f->flk_file)
      break;
    HANDLE fh = *((HANDLE *)f->flk_file->f_handle);
    if (!fh)
      break;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)offset;
    if (FALSE == UnlockFileEx(fh, 0, (DWORD)len, 0, &overlapped)) {
      break;
    }
    r = SK_OK;
  } while (0);
  return r;
}
void *sk_fwlock_get_file(sk_flock_t *h) {
  void *r = NULL;
  do {
    if (!h)
      break;
    if (!h->flk_file)
      break;
    r = h->flk_file->f_file;
  } while (0);
  return r;
}