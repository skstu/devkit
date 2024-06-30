#ifndef CVPP_STREAM_H
#define CVPP_STREAM_H

class Stream final : public ICvpp::IStream {
public:
 Stream();
 Stream(const void*, const size_t&);
 virtual ~Stream();
protected:
 void Release() const override final;
 void SetData(const void*, const size_t&) override final;
 void* GetBuffer() const override final;
 size_t GetSize() const override final;
 bool Empty() const override final;
private:
 void* buffer_ = nullptr;
 size_t buffer_size_ = 0;
};

#endif //CVPP_STREAM_H
