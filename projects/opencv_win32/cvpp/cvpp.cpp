#include "stdafx.h"

Cvpp::Cvpp() {
	Init();
}

Cvpp::~Cvpp() {
	UnInit();
}

void Cvpp::Init() {
	std::cout << cv::getBuildInformation().c_str() << std::endl;
}

void Cvpp::UnInit() {

}