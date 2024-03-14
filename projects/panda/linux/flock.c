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
    r->flk_file = sk_file_open(fpath, "r+");
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
    if (!f->flk_file->f_handle)
      break;
    int fd = *((int *)f->flk_file->f_handle);
    if (fd == -1)
      break;
    struct flock fwlock;
    fwlock.l_type = F_WRLCK;
    fwlock.l_start = offset;
    fwlock.l_len = len;
    fwlock.l_whence = SEEK_SET;
    if (0 != fcntl(fd, F_SETLKW, &fwlock))
      break;
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
    if (!f->flk_file->f_handle)
      break;
    int fd = *((int *)f->flk_file->f_handle);
    if (fd == -1)
      break;
    struct flock fwlock;
    fwlock.l_type = F_WRLCK;
    fwlock.l_start = offset;
    fwlock.l_len = len;
    fwlock.l_whence = SEEK_SET;
    if (0 != fcntl(fd, F_SETLKW, &fwlock))
      break;
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
    if (!f->flk_file->f_handle)
      break;
    int fd = *((int *)f->flk_file->f_handle);
    if (fd == -1)
      break;
    struct flock fwlock;
    fwlock.l_type = F_UNLCK;
    fwlock.l_start = offset;
    fwlock.l_len = len;
    fwlock.l_whence = SEEK_SET;
    if (0 != fcntl(fd, F_SETLKW, &fwlock))
      break;
    r = SK_OK;
  } while (0);
  return r;
}