#ifndef CVPP_H
#define CVPP_H

class Cvpp final : public cvpp::ICvpp {
public:
	Cvpp();
	virtual ~Cvpp();
protected:
	void Stop() override final;
	bool Start() override final;
	bool Ready() const override final;
	void Release() const override final;
private:
	void Init();
	void UnInit();
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

#endif //CVPP_H
