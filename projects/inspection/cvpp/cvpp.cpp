#include "stdafx.h"
using namespace cv;

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <Windows.h>
#define SM_CX 0x00001007 // Desktop width (in pixels)
#define SM_CY 0x00001008 // Desktop height (in pixels)

using namespace cv;

#include <opencv2/opencv.hpp>
#include <Windows.h>
#include <iostream>

// Function to capture the screen
cv::Mat captureScreen() {

  // Get the device context of the screen
  HDC hScreenDC = GetDC(NULL);
  HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  // Create a compatible bitmap from the screen DC
  HBITMAP hBitmap =
      CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);

  // Select the compatible bitmap into the memory DC
  HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

  // Bit block transfer into our compatible memory DC
  BitBlt(hMemoryDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY);

  // Create a bitmap structure and retrieve the screen image
  BITMAP bmpScreen = {0};
  GetObjectW(hBitmap, sizeof(BITMAP), &bmpScreen);

  BITMAPINFOHEADER bi = {0};
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = bmpScreen.bmWidth;
  bi.biHeight = -bmpScreen.bmHeight;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = 0;
  bi.biXPelsPerMeter = 0;
  bi.biYPelsPerMeter = 0;
  bi.biClrUsed = 0;
  bi.biClrImportant = 0;

  // Create a buffer to hold the screen image data
  cv::Mat matScreen(bmpScreen.bmHeight, bmpScreen.bmWidth, CV_8UC4);
  GetDIBits(hMemoryDC, hBitmap, 0, (UINT)bmpScreen.bmHeight, matScreen.data,
            (BITMAPINFO *)&bi, DIB_RGB_COLORS);

  // Clean up
  SelectObject(hMemoryDC, hOldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hMemoryDC);
  ReleaseDC(NULL, hScreenDC);

  return matScreen;
}

#if 0
int main() {
 // Load the icon template image
 cv::Mat iconTemplate = cv::imread("path_to_icon_template.png", cv::IMREAD_COLOR);
 if (iconTemplate.empty()) {
  std::cerr << "Could not load template image!" << std::endl;
  return -1;
 }

 // Capture the screen
 cv::Mat screen = captureScreen();

 // Perform template matching
 cv::Mat result;
 cv::matchTemplate(screen, iconTemplate, result, cv::TM_CCOEFF_NORMED);

 // Localize the best match with minMaxLoc
 double minVal, maxVal;
 cv::Point minLoc, maxLoc;
 cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

 // Draw a rectangle around the matched region (for visualization)
 cv::rectangle(screen, maxLoc, cv::Point(maxLoc.x + iconTemplate.cols, maxLoc.y + iconTemplate.rows), cv::Scalar(0, 255, 0), 2, 8, 0);

 // Display the result
 cv::imshow("Matched Image", screen);
 cv::waitKey(0);

 // Print the coordinates of the top-left corner of the matched region
 std::cout << "Icon found at: (" << maxLoc.x << ", " << maxLoc.y << ")" << std::endl;

 return 0;
}
#endif

// 匹配模板函数
Point2f findImage(const Mat &templateImage) {
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
  // minMaxLoc(resultImage, &minVal, &maxVal, &minLoc, &maxLoc, Mat());

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
  // std::cout << cv::getBuildInformation().c_str() << std::endl;
}

void Cvpp::UnInit() {
}
bool Cvpp::Start() {
  do {
    if (open_.load())
      break;

    open_.store(true);
  } while (0);

  return open_.load();
}
void Cvpp::Stop() {
  do {
    if (!open_.load())
      break;

    open_.store(false);
  } while (0);
}

bool Cvpp::Ready() const {
  return false;
}
void Cvpp::Release() const {
  delete this;
}

// Function to capture the screen image from binary data
void captureScreen(const std::vector<uchar> &imageData, cv::Mat &outputMat) {
  // Convert the binary data to a cv::Mat
  cv::Mat img = cv::imdecode(imageData, cv::IMREAD_COLOR);
  if (img.empty()) {
    std::cerr << "Failed to decode image from binary data!" << std::endl;
    return;
  }

  // Store the decoded image in the output matrix
  outputMat = img.clone();
}
#if 0
int main() {
 // Simulate loading binary data from an external method
 std::vector<uchar> imageData;
 std::ifstream file("screenshot.png", std::ios::binary);
 if (file) {
  file.unsetf(std::ios::skipws);
  file.seekg(0, std::ios::end);
  std::streampos fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  imageData.reserve(fileSize);
  imageData.insert(imageData.begin(), std::istream_iterator<uchar>(file), std::istream_iterator<uchar>());
 }
 else {
  std::cerr << "Failed to read screenshot.png file!" << std::endl;
  return -1;
 }

 // Load the icon template image
 cv::Mat iconTemplate = cv::imread("path_to_icon_template.png", cv::IMREAD_COLOR);
 if (iconTemplate.empty()) {
  std::cerr << "Could not load template image!" << std::endl;
  return -1;
 }

 // Capture the screen image from binary data
 cv::Mat screen;
 captureScreen(imageData, screen);
 if (screen.empty()) {
  std::cerr << "Failed to capture screen from binary data!" << std::endl;
  return -1;
 }

 // Perform template matching
 cv::Mat result;
 cv::matchTemplate(screen, iconTemplate, result, cv::TM_CCOEFF_NORMED);

 // Localize the best match with minMaxLoc
 double minVal, maxVal;
 cv::Point minLoc, maxLoc;
 cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

 // Draw a rectangle around the matched region (for visualization)
 cv::rectangle(screen, maxLoc, cv::Point(maxLoc.x + iconTemplate.cols, maxLoc.y + iconTemplate.rows), cv::Scalar(0, 255, 0), 2, 8, 0);

 // Display the result
 cv::imshow("Matched Image", screen);
 cv::waitKey(0);

 // Print the coordinates of the top-left corner of the matched region
 std::cout << "Icon found at: (" << maxLoc.x << ", " << maxLoc.y << ")" << std::endl;

 return 0;
}
#endif

void Cvpp::TestCv(const char *mainImg, size_t mainImgSize, const char *childImg,
                  size_t childImgSize) const {
  do {
    if (!mainImg || mainImgSize <= 0 || !childImg || childImgSize <= 0)
      break;

    cv::Mat mainData = cv::imdecode(
        std::vector<uchar>(mainImg, mainImg + mainImgSize), cv::IMREAD_COLOR);
    // cv::Mat mainData = captureScreen();
    cv::Mat childTemplate =
        cv::imdecode(std::vector<uchar>(childImg, childImg + childImgSize),
                     cv::IMREAD_COLOR);

    cv::Mat result;
    cv::matchTemplate(mainData, childTemplate, result, cv::TM_CCOEFF_NORMED);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

    cv::rectangle(
        mainData, maxLoc,
        cv::Point(maxLoc.x + childTemplate.cols, maxLoc.y + childTemplate.rows),
        cv::Scalar(0, 255, 0), 2, 8, 0);

    // cv::imshow("Matched Image", mainData);
    // cv::waitKey(0);
    auto sk = 0;
  } while (0);
}
Cvpp::IRange *Cvpp::Find(const Cvpp::IStream *src, const Cvpp::IStream *dst,
                         double threshold_value) const {
  Cvpp::IRange *result = nullptr;
  if (threshold_value <= 0)
    threshold_value = 0.3;
  do {
    if (!src || !dst)
      break;
    if (src->Empty() || dst->Empty())
      break;
    uchar *pSrc = reinterpret_cast<uchar *>(src->GetBuffer());
    uchar *pDst = reinterpret_cast<uchar *>(dst->GetBuffer());
    cv::Mat srcMat = cv::imdecode(
        std::vector<uchar>(pSrc, pSrc + src->GetSize()), cv::IMREAD_COLOR);
    cv::Mat dstMat = cv::imdecode(
        std::vector<uchar>(pDst, pDst + dst->GetSize()), cv::IMREAD_COLOR);

    if (srcMat.empty() || dstMat.empty())
      break;

    cv::Mat resultMat;
    cv::matchTemplate(srcMat, dstMat, resultMat, cv::TM_CCOEFF_NORMED);
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(resultMat, &minVal, &maxVal, &minLoc, &maxLoc);

    if (maxVal >= threshold_value) { // 设定一个阈值，确保匹配度足够高
      result = dynamic_cast<Cvpp::IRange *>(
          new ::Range(maxLoc.x, maxLoc.y, dstMat.cols, dstMat.rows));

      // 如果需要绘制矩形用于调试
      // cv::rectangle(srcMat, maxLoc, cv::Point(maxLoc.x + dstMat.cols,
      // maxLoc.y + dstMat.rows), cv::Scalar(0, 255, 0), 2, 8, 0);
    }

  } while (0);

  return result;
}

Cvpp::IStream *Cvpp::CreateStream() const {
  return dynamic_cast<Cvpp::IStream *>(new Stream());
}
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
Cvpp *__gpCvpp = nullptr;
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpCvpp) {
      break;
    }
    __gpCvpp = new Cvpp();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<ICvpp *>(__gpCvpp));
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