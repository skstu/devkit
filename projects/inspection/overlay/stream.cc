#include "stdafx.h"

Stream::Stream() {
  data_ = nullptr;
  data_len_ = 0;
}
Stream::~Stream() {
  SK_FREE_PTR(data_);
}
void Stream::SetData(const void *data, const size_t &len) {
  do {
    if (!data || len <= 0)
      break;
    SK_FREE_PTR(data_);
    data_ = (char *)malloc(len);
    data_len_ = len;
    memcpy(data_, data, data_len_);
  } while (0);
}
void *Stream::GetBuffer() const {
  return data_;
}
size_t Stream::GetSize() const {
  return data_len_;
}
void Stream::Release() const {
  delete this;
}
bool Stream::Empty() const {
  if (!data_ || data_len_ <= 0)
    return true;
  return false;
}