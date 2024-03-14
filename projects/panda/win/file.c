#include <stdafx.h>

sk_file_t *sk_file_open(const char *fpath, const char *mode) {
  int r = 0;
  sk_file_t *f = NULL;
  do {
    if (!fpath)
      break;
    f = (sk_file_t *)sk__malloc(sizeof(*f));
    if (!f)
      break;
    f->f_file = (void *)fopen(fpath, mode ? mode : "ab+");
    if (!f->f_file)
      break;
    f->f_path = sk__strdup(fpath);
    if (!f->f_path)
      break;
    HANDLE fh = (HANDLE)_get_osfhandle(_fileno((FILE *)f->f_file));
    if (!fh)
      break;
    f->f_handle = sk__memndup(&fh, sizeof(HANDLE));
    if (!f->f_handle)
      break;
    r = 1;
  } while (0);
  if (!r && !f)
    sk_file_close(&f);
  return f;
}
int sk_file_close(sk_file_t **ph) {
  int r = SK_NO;
  do {
    if (!*ph)
      break;
    sk__free_s((void **)&(*ph)->f_path);
    sk__free_s((void **)&(*ph)->f_handle);
    if (!(*ph)->f_file) {
      r = SK_OK;
      break;
    }
    if (!fclose((*ph)->f_file))
      r = SK_OK;
    sk__free_s((void **)ph);
  } while (0);
  return r;
}
int sk_file_write(sk_file_t *h, const char *buf, size_t len) {
  int r = SK_NO;
  do {
    if (!h || !buf || len <= 0)
      break;
    size_t wlen = fwrite(buf, sizeof(char), len, (FILE *)h->f_file);
    if (len != wlen)
      break;
    r = SK_OK;
  } while (0);
  return r;
}
int sk_file_read(sk_file_t *h, char **buf, size_t *len) {
  int r = SK_NO;
  *buf = NULL;
  *len = 0;
  do {
    if (!h)
      break;
    if (!h->f_file)
      break;
    if (0 != fseek((FILE *)h->f_file, 0, SEEK_END))
      break;
    size_t total = ftell((FILE *)h->f_file);
    if (total <= 0) {
      if (errno == 0) {
        r = SK_OK;
      }
      break;
    }
    if (0 != fseek((FILE *)h->f_file, 0, SEEK_SET))
      break;
    *buf = (char *)sk__malloc(total);
    if (!*buf)
      break;
    *len = fread(*buf, sizeof(char), total, (FILE *)h->f_file);
    if (*len != total)
      break;
    r = SK_OK;
  } while (0);
  if (r != SK_OK) {
    *len = 0;
    sk__free_s((void **)buf);
  }
  return r;
}