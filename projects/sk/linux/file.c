#include <stdafx.h>
/*
基本模式：

"r"：以只读方式打开文件。
"w"：以只写方式打开文件。
"a"：以追加方式打开文件。
读写模式：

"r+"：以读写方式打开文件。
"w+"：以读写方式打开文件，并截断文件内容。
"a+"：以读写方式打开文件，并追加到文件末尾。
二进制/文本模式：

"rb"：以二进制方式打开文件。
"wb"：以二进制方式打开文件，并截断文件内容。
"ab"：以二进制方式打开文件，并追加到文件末尾。
"rt"：以文本方式打开文件。
"wt"：以文本方式打开文件，并截断文件内容。
"at"：以文本方式打开文件，并追加到文件末尾。
组合模式：

"r+b"：以读写方式打开文件，并以二进制方式进行读写。
"w+b"：以读写方式打开文件，并以二进制方式进行读写，并截断文件内容。
"a+b"：以读写方式打开文件，并以二进制方式进行读写，并追加到文件末尾。
"r+t"：以读写方式打开文件，并以文本方式进行读写。
"w+t"：以读写方式打开文件，并以文本方式进行读写，并截断文件内容。
"a+t"：以读写方式打开文件，并以文本方式进行读写，并追加到文件末尾。
注意：

并非所有平台都支持所有模式。
在使用二进制模式时，请确保文件内容与所使用的平台兼容。

总结 :
只读模式：r、rb
只写模式：w、wb
读写模式：r+(开头)、rb+(开头、读写、二进制)、w+(开头、清空)、wb+(开头、读写、清空、二进制)
追加模式：a、ab
其他模式：x、xb
*/
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
    int fd = fileno((FILE *)f->f_file);
    if (fd < 0)
      break;
    f->f_handle = sk__memndup(&fd, sizeof(int));
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