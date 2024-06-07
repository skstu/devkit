#ifndef CVPP_H
#define CVPP_H

class Cvpp final : public cvpp::ICvpp {
public:
	Cvpp();
	virtual ~Cvpp();
private:
	void Init();
	void UnInit();
};

#endif //CVPP_H
