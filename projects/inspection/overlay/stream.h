#ifndef __PROJECTS_INSPECTION_OVERLAY_STREAM_H_
#define __PROJECTS_INSPECTION_OVERLAY_STREAM_H_

class Stream final : public IOverlay::IStream {
public:
  Stream();
  virtual ~Stream();

public:
  void SetData(const void *, const size_t &) override final;
  void *GetBuffer() const override final;
  size_t GetSize() const override final;
  void Release() const override final;
  bool Empty() const override final;

private:
  char *data_ = nullptr;
  size_t data_len_ = 0;
};
#endif //__PROJECTS_INSPECTION_OVERLAY_STREAM_H_