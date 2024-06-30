#include "stdafx.h"

using namespace Gdiplus;
POINT ptLastPos = { 0, 0 };  // 用于记录上一次鼠标的位置
bool isDragging = false;   // 用于标记是否正在拖动
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void SaveBitmapAsPng(HBITMAP hBitmap, const std::wstring& filename)
{
 CImage image;
 image.Attach(hBitmap);
 image.Save(filename.c_str(), Gdiplus::ImageFormatPNG);
 image.Detach();
}

void CaptureWindow(HWND hWnd)
{
 RECT rect;
 GetWindowRect(hWnd, &rect);
 int width = rect.right - rect.left;
 int height = rect.bottom - rect.top;

 HDC hdcScreen = GetDC(nullptr);
 HDC hdcMem = CreateCompatibleDC(hdcScreen);
 HBITMAP hbmCapture = CreateCompatibleBitmap(hdcScreen, width, height);
 SelectObject(hdcMem, hbmCapture);

 BitBlt(hdcMem, 0, 0, width, height, hdcScreen, rect.left, rect.top, SRCCOPY | CAPTUREBLT);

 // Create a circular region
 HDC hdcCircle = CreateCompatibleDC(hdcScreen);
 HBITMAP hbmCircle = CreateCompatibleBitmap(hdcScreen, width, height);
 SelectObject(hdcCircle, hbmCircle);

 HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
 SelectObject(hdcCircle, hBrush);
 Ellipse(hdcCircle, 0, 0, width, height);

 HBITMAP hbmMask = CreateCompatibleBitmap(hdcScreen, width, height);
 HDC hdcMask = CreateCompatibleDC(hdcScreen);
 SelectObject(hdcMask, hbmMask);

 BitBlt(hdcMask, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
 BitBlt(hdcMask, 0, 0, width, height, hdcCircle, 0, 0, SRCAND);

 // Use the mask to create a circular image
 HBITMAP hbmFinal = CreateCompatibleBitmap(hdcScreen, width, height);
 HDC hdcFinal = CreateCompatibleDC(hdcScreen);
 SelectObject(hdcFinal, hbmFinal);

 BitBlt(hdcFinal, 0, 0, width, height, hdcMem, 0, 0, SRCINVERT);
 BitBlt(hdcFinal, 0, 0, width, height, hdcMask, 0, 0, SRCAND);
 BitBlt(hdcFinal, 0, 0, width, height, hdcMem, 0, 0, SRCINVERT);

 SaveBitmapAsPng(hbmFinal, L"circle_screenshot.png");

 DeleteObject(hbmCapture);
 DeleteObject(hbmMask);
 DeleteObject(hbmCircle);
 DeleteObject(hbmFinal);
 DeleteDC(hdcMem);
 DeleteDC(hdcCircle);
 DeleteDC(hdcMask);
 DeleteDC(hdcFinal);
 ReleaseDC(nullptr, hdcScreen);
}
void DrawCircle(HDC hdc, int width, int height)
{
 Graphics graphics(hdc);
 graphics.SetSmoothingMode(SmoothingModeAntiAlias); // 启用抗锯齿
 Color color(50, 255, 0, 0); // 半透明红色 (128 表示半透明)
 SolidBrush brush(color);

 graphics.FillEllipse(&brush, 0, 0, width, height);
}

void RedrawWindow(HWND hWnd)
{
 RECT rect;
 GetClientRect(hWnd, &rect);
 int width = rect.right - rect.left;
 int height = rect.bottom - rect.top;

 HDC hdc = GetDC(hWnd);
 HDC hdcMem = CreateCompatibleDC(hdc);
 HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
 HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

 // Clear background
 HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
 FillRect(hdcMem, &rect, hBrush);
 DeleteObject(hBrush);

 // Draw circle
 DrawCircle(hdcMem, width, height);

 POINT ptSrc = { 0, 0 };
 SIZE sizeWnd = { width, height };
 BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

 POINT ptDst;
 GetCursorPos(&ptDst);
 ptDst.x -= (width / 2);
 ptDst.y -= (height / 2);
 //UpdateLayeredWindow(hWnd, hdc, nullptr, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
 UpdateLayeredWindow(hWnd, hdc, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

 SelectObject(hdcMem, hbmOld);
 DeleteObject(hbmMem);
 DeleteDC(hdcMem);
 ReleaseDC(hWnd, hdc);
}
void SetCircularRegion(HWND hWnd) {
 RECT rect;
 GetWindowRect(hWnd, &rect);
 int width = rect.right - rect.left;
 int height = rect.bottom - rect.top;
 HRGN hRgn = CreateEllipticRgn(0, 0, width, height);
 SetWindowRgn(hWnd, hRgn, TRUE);
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
 switch (message)
 {
 case WM_CREATE:
 {
  SetCircularRegion(hWnd);
  RedrawWindow(hWnd);
  return 0;
 }
 case WM_SIZE:
 case WM_SIZING: {
  SetCircularRegion(hWnd);
  RedrawWindow(hWnd);
 }break;
 case WM_LBUTTONUP:
 {
  isDragging = false;
  ReleaseCapture();
  break;
 }
 case WM_PAINT: {
  RedrawWindow(hWnd);
 }break;
 case WM_DESTROY:
  PostQuitMessage(0);
  break;
 default:
  return DefWindowProc(hWnd, message, wParam, lParam);
 }
 return 0;
}



Overlay::Overlay() {
 Init();
}
Overlay:: ~Overlay() {
 UnInit();
}

void Overlay::Init() {
 GdiplusStartupInput gdiplusStartupInput;
 GdiplusStartup(&gdiplustoken_, &gdiplusStartupInput, nullptr);
}
void Overlay::UnInit() {
 GdiplusShutdown(gdiplustoken_);
}

bool Overlay::Start() {
 do {
  if (open_.load())
   break;
  uiohook_ = IUiohook::Create(R"(C:\Users\k34ub\source\skstu\devkit\out\uiohook.dll)");
  if (!uiohook_->Start())
   break;
  uiohook_->RegisterMouseMoveCb(OnMouseMove, this);
  open_.store(true);
  threads_.emplace_back([this]() {WindowThread(); });
 } while (0);
 return open_.load();
}
void Overlay::Stop() {
 do {
  if (!open_.load())
   break;


  open_.store(false);
  for (auto& t : threads_)
   t.join();
  threads_.clear();
  uiohook_->Stop();
  IUiohook::Destroy(&uiohook_);
 } while (0);
}
bool Overlay::Ready() const {
 return open_.load();
}
void Overlay::Release() const {
 delete this;
}
bool Overlay::Screenshot(const std::function<void(const char*, const size_t&)>&) const {
 return false;
}
void Overlay::OnMouseMove(const long& x, const long& y, void* _this) {
 Overlay* pOverlay = reinterpret_cast<Overlay*>(_this);
 do {
  if (!pOverlay)
   break;
  //std::cout << x << "<>" << y << std::endl;
  //RedrawWindow(pOverlay->hwnd_, x, y);

  //SetWindowPos(pOverlay->hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE);
  InvalidateRect(pOverlay->hwnd_, NULL, TRUE);
 } while (0);
}

static UINT GetDPI(const HWND& hWnd) {
 UINT dpiX = 0;
 UINT dpiY = 0;
 HDC hdc = nullptr;
 do {
  if (!hWnd)
   break;
  /*if (SUCCEED(GetDpiForWindow(hWnd, &dpiX, &dpiY)))
    break;*/
  hdc = GetDC(hWnd);
  dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
 } while (0);
 if (hdc)
  ReleaseDC(hWnd, hdc);
 return dpiX;
}
static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam,
 LPARAM lParam) {
 auto _this =
  reinterpret_cast<Overlay*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

 static int mouseX = 0;
 static int mouseY = 0;
 static bool trackingMouse = false;
 static float dpiScale = 1.0f;
 switch (uMsg) {
 case WM_CREATE: {
  LRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  int dpi = GetDPI(hWnd);
  dpiScale = dpi / 96.0f;
  return FALSE;
 } break;
 case WM_PAINT: {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hWnd, &ps);

  // 使用双缓冲技术绘图
  RECT clientRect;
  GetClientRect(hWnd, &clientRect);
  HDC hdcMem = CreateCompatibleDC(hdc);
  HBITMAP hbmMem =
   CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
  HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

  // 清除背景
  Gdiplus::Graphics graphicsMem(hdcMem);
  graphicsMem.Clear(Gdiplus::Color(0, 0, 0, 0)); // 透明背景

  int left = clientRect.left;
  int top = clientRect.top;
  int width = clientRect.right - clientRect.left - 1;
  int height = clientRect.bottom - clientRect.top - 1;

  Gdiplus::Pen pen(Gdiplus::Color(255, 255, 215, 0), 2);
  graphicsMem.DrawRectangle(&pen, left, top, width, height);

  BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0,
   SRCCOPY);

  SelectObject(hdcMem, hOld);
  DeleteObject(hbmMem);
  DeleteDC(hdcMem);

  EndPaint(hWnd, &ps);
 } break;
 case WM_ERASEBKGND: {
  return TRUE;
 }
 case WM_DESTROY: {
  PostQuitMessage(0);
  // std::cout << "Overlay module exit." << std::endl;
 } break;
 default:
  return DefWindowProc(hWnd, uMsg, wParam, lParam);
 }
 return 0;
}

void Overlay::WindowThread() {
 do {
  WNDCLASSEXW wcex = { 0 };
  wcex.cbSize = sizeof(WNDCLASSEXW);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = GetModuleHandleW(NULL);
  wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = nullptr;
  wcex.lpszClassName = class_name_.c_str();
  wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

  RegisterClassExW(&wcex);

  HWND hwnd_ = CreateWindowExW(
   WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
   class_name_.c_str(),
   NULL,
   WS_POPUP,
   CW_USEDEFAULT,
   CW_USEDEFAULT,
   100,
   100,
   NULL,
   NULL,
   GetModuleHandleW(NULL), NULL);

  if (!hwnd_)
   break;

  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  /*if (FALSE ==
   SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY))
   break;*/

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);

  MSG msg = { 0 };
  do {
   if (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE) != 0) {
    if (msg.message == WM_QUERYENDSESSION || msg.message == WM_CLOSE ||
     msg.message == WM_DESTROY || msg.message == WM_QUIT ||
     msg.message == WM_ENDSESSION) {
     SendMessageW(hwnd_, WM_DESTROY, 0, 0);
     break;
    }
    else {
     TranslateMessage(&msg);
     DispatchMessageW(&msg);
    }
   }
   if (!open_.load()) {
    SendMessageW(hwnd_, WM_DESTROY, 0, 0);
    break;
   }
   else {


   }
   std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (1);

 } while (0);


#if 0
 do {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = OverlayWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = CLASS_NAME;
  RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
   WS_EX_TOOLWINDOW, // 添加 WS_EX_TOOLWINDOW
   CLASS_NAME, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL,
   GetModuleHandleW(NULL), NULL);
  if (!hwnd_)
   break;

  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  if (FALSE ==
   SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY))
   break;

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);

  MSG msg = { 0 };
  do {
   if (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE) != 0) {
    if (msg.message == WM_QUERYENDSESSION || msg.message == WM_CLOSE ||
     msg.message == WM_DESTROY || msg.message == WM_QUIT ||
     msg.message == WM_ENDSESSION) {
     SendMessageW(hwnd_, WM_DESTROY, 0, 0);
     break;
    }
    else {
     TranslateMessage(&msg);
     DispatchMessageW(&msg);
    }
   }
   if (!open_.load()) {
    SendMessageW(hwnd_, WM_DESTROY, 0, 0);
    break;
   }
   else {


   }
   std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (1);
 } while (0);
#endif
}