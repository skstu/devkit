#include "stdafx.h"

Stream::Stream() : buffer_(nullptr), buffer_size_(0) {

}
Stream::Stream(const void* _buf, const size_t& _len) {
 SetData(_buf, _len);
}
Stream::~Stream() {
 SK_FREE_PTR(buffer_);
}
void Stream::Release() const {
 delete this;
}
bool Stream::Empty() const {
 return buffer_size_ <= 0;
}
void Stream::SetData(const void* buffer, const size_t& size) {
 do {
  if (!buffer || size <= 0)
   break;
  SK_FREE_PTR(buffer_);
  buffer_size_ = size;
  buffer_ = malloc(buffer_size_);
  if (!buffer_) {
   buffer_size_ = 0;
   break;
  }
  memcpy(buffer_, buffer, size);
 } while (0);


}
void* Stream::GetBuffer() const {
 return buffer_;
}
size_t Stream::GetSize() const {
 return buffer_size_;
}