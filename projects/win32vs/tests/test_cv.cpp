#include "test.h"

cvpp::ICvpp* __gpCvpp = nullptr;

void DrawRectangleOnDesktop(int x, int y, int width, int height) {
 HDC hdc = GetDC(NULL); // 获取屏幕设备上下文
 if (hdc) {
  HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0)); // 创建红色画笔
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH)); // 选择空刷子，确保矩形内部是透明的

  Rectangle(hdc, x, y, x + width, y + height); // 画矩形

  SelectObject(hdc, oldPen); // 恢复旧的画笔
  SelectObject(hdc, oldBrush); // 恢复旧的刷子
  DeleteObject(pen); // 删除画笔
  ReleaseDC(NULL, hdc); // 释放设备上下文
 }
}

void test_cv() {
 __gpCvpp = cvpp::ICvpp::Create("cvpp.dll");

 __gpCvpp->Start();

 std::string mainImg = stl::File::ReadFile(R"(C:\Users\k34ub\Desktop\src.bmp)");
 std::string childImg = stl::File::ReadFile(R"(C:\Users\k34ub\Desktop\dest.bmp)");
 auto streamSrc = __gpCvpp->CreateStream();
 auto streamDst = __gpCvpp->CreateStream();
 streamSrc->SetData(mainImg.data(), mainImg.size());
 streamDst->SetData(childImg.data(), childImg.size());
 auto found = __gpCvpp->Find(streamSrc, streamDst, 0.3);

 streamSrc->Release();
 streamDst->Release();
 if (found) {
  DrawRectangleOnDesktop(found->X(), found->Y(), found->CX(), found->CY());
  found->Release();
 }

 std::string input;
 do {
  input.clear();
  std::getline(std::cin, input);
  if (input == "q" || std::cin.eof()) {
   __gpCvpp->Stop();
   break;
  }
 } while (1);
 cvpp::ICvpp::Destroy(&__gpCvpp);
}
