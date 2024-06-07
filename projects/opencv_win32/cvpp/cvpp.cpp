#include "stdafx.h"
using namespace cv;

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <Windows.h>
#define SM_CX 0x00001007 // Desktop width (in pixels)
#define SM_CY 0x00001008 // Desktop height (in pixels)


using namespace cv;

// 匹配模板函数
Point2f findImage(const Mat& templateImage) {
#if 0
    // 获取桌面截图
    Mat screenImage;
    HDC hdc = GetDC(NULL);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, GetSystemMetrics(SM_CX), GetSystemMetrics(SM_CY));

    BitBlt(hBitmap, 0, 0, GetSystemMetrics(SM_CX), GetSystemMetrics(SM_CY), hdc, 0, 0, SRCCPY);
    BITMAP bitmap;
    GetObject(hBitmap, sizeof(bitmap), &bitmap);
    screenImage = Mat(bitmap.bmHeight, bitmap.bmWidth, CV_8UC3, (BYTE*)bitmap.bmBits);
    DeleteObject(hBitmap);
    ReleaseDC(NULL, hdc);

    // 转换为灰度图
    Mat grayImage, grayTemplateImage;
    cvtColor(screenImage, grayImage, CV_BGR2GRAY);
    cvtColor(templateImage, grayTemplateImage, CV_BGR2GRAY);

    // 匹配模板
    Mat resultImage;
    matchTemplate(grayImage, grayTemplateImage, resultImage, CV_TM_SQDIFF_NORMED);
    // 找到最佳匹配位置
    double minVal, maxVal;
#endif

    Point minLoc, maxLoc;
    //minMaxLoc(resultImage, &minVal, &maxVal, &minLoc, &maxLoc, Mat());

    // 返回匹配图标的坐标
    return minLoc;
}

#if 0
int main() {
    // 加载模板图像
    Mat templateImage = imread("template.png");
    if (templateImage.empty()) {
        std::cout << "无法加载模板图像" << std::endl;
        return 1;
    }

    // 匹配图标并获取坐标
    Point2f iconPosition = findImage(templateImage);

    // 输出图标坐标
    std::cout << "图标坐标: (" << iconPosition.x << ", " << iconPosition.y << ")" << std::endl;

    return 0;
}
#endif



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
void Cvpp::Stop() {

}
bool Cvpp::Start() {
	return false;
}
bool Cvpp::Ready() const {
	return false;
}
void Cvpp::Release() const {
	delete this;
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Cvpp* __gpCvpp = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
	SHARED_API void* interface_init(void* data, unsigned long len) {
		void* result = nullptr;
		do {
			if (__gpCvpp) {
				break;
			}
			__gpCvpp = new Cvpp();
		} while (0);
		return reinterpret_cast<void*>(dynamic_cast<Cvpp*>(__gpCvpp));
	}
	SHARED_API void interface_uninit() {
		do {
			if (!__gpCvpp) {
				break;
			}
			delete __gpCvpp;
			__gpCvpp = nullptr;
		} while (0);
	}

#ifdef __cplusplus
}
#endif