#ifndef CVPP_CVPP_H
#define CVPP_CVPP_H

class Cvpp final : public ICvpp {
public:
	Cvpp();
	virtual ~Cvpp();
protected:
	void Stop() override final;
	bool Start() override final;
	bool Ready() const override final;
	void Release() const override final;
	IStream* CreateStream() const override final;
	IRange* Find(const IStream* ,const IStream*,  double threshold_value) const override final;
	void TestCv(const char*, size_t, const char*, size_t) const override final;
private:
	void Init();
	void UnInit();
private:
	std::atomic_bool open_ = false;
};

#ifdef __cplusplus
extern "C" {
#endif
	SHARED_API void* interface_init(void*, unsigned long);
	SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Cvpp* __gpCvpp;

#endif //CVPP_CVPP_H
