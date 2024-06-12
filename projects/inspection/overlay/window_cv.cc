#include "stdafx.h"

using namespace Gdiplus;
POINT ptLastPos = {0, 0}; // 用于记录上一次鼠标的位置
bool isDragging = false;  // 用于标记是否正在拖动

static std::atomic_long gs_windowcv_mouse_wheel_count = 0;
static std::atomic_bool gs_windowcv_screenshot_switch = false;
#if 1
bool __Screenshot(const HWND &hwnd) {
  gs_windowcv_screenshot_switch.store(false);
  bool result = false;
  std::vector<char> imgBuffer;
  imgBuffer.clear();
  IStream *pStream = nullptr;
  HBITMAP hBitmap = nullptr;
  HDC hScreenDC = nullptr;
  HDC hMemoryDC = nullptr;
  do {
    RECT rect;
    GetWindowRect(hwnd, &rect);

    hScreenDC = GetDC(NULL);
    hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hScreenDC || !hMemoryDC)
      break;
    hBitmap = CreateCompatibleBitmap(hScreenDC, rect.right - rect.left,
                                     rect.bottom - rect.top);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    if (!hBitmap || !hOldBitmap)
      break;
    if (FALSE == BitBlt(hMemoryDC, 0, 0, rect.right - rect.left,
                        rect.bottom - rect.top, hScreenDC, rect.left, rect.top,
                        SRCCOPY))
      break;
    Gdiplus::Bitmap bitmap(hBitmap, NULL);
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &pStream)) || !pStream)
      break;
    CLSID clsid;
    if (FAILED(
            CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid)))
      break;
    if (Gdiplus::Status::Ok != bitmap.Save(pStream, &clsid, NULL))
      break;
    STATSTG statstg = {0};
    if (FAILED(pStream->Stat(&statstg, STATFLAG_NONAME)))
      break;
    ULONG streamSize = statstg.cbSize.LowPart;
    imgBuffer.resize(streamSize);
    //*len = streamSize;
    //*buffer = (char *)malloc(*len);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
    ULONG bytesRead = 0;
    pStream->Read(&imgBuffer[0], streamSize, &bytesRead);
    imgBuffer.resize(bytesRead);
    SelectObject(hMemoryDC, hOldBitmap);
    result = true;
  } while (0);
  if (hBitmap)
    DeleteObject(hBitmap);
  if (hMemoryDC)
    DeleteDC(hMemoryDC);
  if (hScreenDC)
    ReleaseDC(NULL, hScreenDC);
  SK_RELEASE_PTR(pStream);

  do {
    if (imgBuffer.empty())
      break;
#if _DEBUG
    stl::File::WriteFile(R"(C:\Users\k34ub\Desktop\dst.png)",
                         std::string(&imgBuffer[0], imgBuffer.size()));
#else
    std::string path = sk::GetCurrentPath();
    if (!stl::File::WriteFile(fmt::format("{}\\{}.png", path, "dst"),
                              std::string(&imgBuffer[0], imgBuffer.size())))
      break;
#endif
    std::cout << "Screenshot success." << std::endl;
  } while (0);
  return result;
}
#else
bool __Screenshot(const HWND &hwnd) {
  gs_windowcv_screenshot_switch.store(false);
  bool result = false;
  HDC hdcScreen = nullptr;
  HDC hdcMem = nullptr;
  HBITMAP hbmCapture = nullptr;
  IStream *pStream = nullptr;
  std::vector<char> imgBuffer;

  do {
    RECT rect;
    if (FALSE == GetWindowRect(hwnd, &rect))
      break;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    hdcScreen = GetDC(nullptr);
    if (!hdcScreen)
      break;
    hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem)
      break;
    hbmCapture = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hbmCapture)
      break;
    SelectObject(hdcMem, hbmCapture);

    if (FALSE == BitBlt(hdcMem, 0, 0, width, height, hdcScreen, rect.left,
                        rect.top, SRCCOPY | CAPTUREBLT))
      break;

    // Create a GDI+ bitmap from the HBITMAP
    Gdiplus::Bitmap bitmap(hbmCapture, nullptr);
    if (bitmap.GetLastStatus() != Gdiplus::Ok)
      break;

    // Create a new bitmap with ARGB format to support transparency
    Gdiplus::Bitmap finalBitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&finalBitmap);

    // Make the entire bitmap transparent
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Create a circular region
    Gdiplus::GraphicsPath path;
    path.AddEllipse(0, 0, width, height);

    // Only keep the content within the circular region
    graphics.SetClip(&path, Gdiplus::CombineModeReplace);
    graphics.DrawImage(&bitmap, 0, 0);

    // Save the image to a stream
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &pStream)) || !pStream)
      break;
    CLSID clsid;
    if (FAILED(CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}",
                               &clsid))) // Use PNG CLSID
      break;
    if (finalBitmap.Save(pStream, &clsid, NULL) != Gdiplus::Ok)
      break;
    STATSTG statstg = {0};
    if (FAILED(pStream->Stat(&statstg, STATFLAG_NONAME)))
      break;
    ULONG streamSize = statstg.cbSize.LowPart;
    imgBuffer.resize(streamSize);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
    ULONG bytesRead = 0;
    pStream->Read(&imgBuffer[0], streamSize, &bytesRead);
    imgBuffer.resize(bytesRead);

    result = true;
  } while (0);

  // Save the image buffer to a file
  do {
    if (imgBuffer.empty())
      break;
#if _DEBUG
    std::ofstream file(R"(C:\Users\k34ub\Desktop\dst.png)", std::ios::binary);
    file.write(&imgBuffer[0], imgBuffer.size());
#else
    std::string path = sk::GetCurrentPath();
    std::ofstream file(fmt::format("{}\\{}.png", path, "dst"),
                       std::ios::binary);
    if (!file)
      break;
    file.write(&imgBuffer[0], imgBuffer.size());
#endif
    std::cout << "Screenshot success." << std::endl;
  } while (0);

  // Cleanup
  if (hbmCapture)
    DeleteObject(hbmCapture);
  if (hdcMem)
    DeleteDC(hdcMem);
  if (hdcScreen)
    ReleaseDC(nullptr, hdcScreen);
  if (pStream)
    pStream->Release();

  return result;
}
#endif

void DrawCircle(HDC hdc, int width, int height, const bool &IsScreenshot) {
  Graphics graphics(hdc);
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  // Color color(20, 255, 215, 0);//!@ 土豪金
  Color color(IsScreenshot ? 0 : 50, 183, 110, 121); //!@ 玫瑰金
  SolidBrush brush(color);

  graphics.FillEllipse(&brush, 0, 0, width, height);
}

void RedrawWindow(HWND hWnd, const bool &IsScreenshot = false) {
  RECT rect;
  GetWindowRect(hWnd, &rect);
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
  DrawCircle(hdcMem, width, height, IsScreenshot);

  POINT ptSrc = {0, 0};
  SIZE sizeWnd = {width, height};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

  POINT ptDst;
  GetCursorPos(&ptDst);
  ptDst.x -= (width / 2);
  ptDst.y -= (height / 2);
  // UpdateLayeredWindow(hWnd, hdc, nullptr, &sizeWnd, hdcMem, &ptSrc, 0,
  // &blend, ULW_ALPHA);
  UpdateLayeredWindow(hWnd, hdc, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend,
                      ULW_ALPHA);

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

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  static WindowCv *_this = reinterpret_cast<WindowCv *>(lParam);
  switch (message) {
  case WM_CREATE: {
    SetCircularRegion(hWnd);
    RedrawWindow(hWnd);
    return FALSE;
  }
  case WM_SIZE:
  case WM_SIZING: {
    SetCircularRegion(hWnd);
    RedrawWindow(hWnd);
  } break;
  /*case WM_LBUTTONUP: {
    isDragging = false;
    ReleaseCapture();
    break;
  }*/
  case WM_PAINT: {
    if (!_this)
      break;
    if (!gs_windowcv_screenshot_switch.load()) {
      RedrawWindow(hWnd);
    } else {
      RedrawWindow(hWnd, true);
      __Screenshot(hWnd);
    }

  } break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

WindowCv::WindowCv()
    : IWindow(OverlayWindowType::OVERLAY_WINDOW_CV, L"OverlayWindowClassFromCv",
              WS_POPUP,
              WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                  WS_EX_TOOLWINDOW,
              WindowProc) {
  Init();
}

WindowCv::~WindowCv() {
  UnInit();
}
void WindowCv::Init() {
  default_window_point_.x = 0;
  default_window_point_.y = 0;
  default_window_size_.cx = 100;
  default_window_size_.cy = 100;
}
void WindowCv::UnInit() {
}

bool WindowCv::Screenshot() const {
  bool result = false;
  do {
    if (!hwnd_)
      break;
    if (FALSE == InvalidateRect(hwnd_, NULL, FALSE))
      break;
    if (FALSE == UpdateWindow(hwnd_))
      break;
    gs_windowcv_screenshot_switch.store(true);
    SendMessageW(hwnd_, WM_PAINT, 0, 0);
    result = true;
  } while (0);
  return result;
}
void WindowCv::Release() const {
  delete this;
}
void WindowCv::OnUioEvent(const uiohook::IUioEvent *pEvent) const {
  switch (pEvent->Type()) {
  case uiohook::UioEventType::UIO_EVENT_MOUSE_MOVED:
    InvalidateRect(hwnd_, NULL, TRUE);
    break;
  case uiohook::UioEventType::UIO_EVENT_MOUSE_WHEEL: {
    /*if (!pEvent->IsKeyPressed(uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_L)
      && !pEvent->IsKeyPressed(uiohook::UioVirtualKeyCodes::UIO_VC_CONTROL_R))
      break;*/
    if (!pEvent->IsKeyPressed(uiohook::UioVirtualKeyCodes::UIO_VC_SHIFT_L) &&
        !pEvent->IsKeyPressed(uiohook::UioVirtualKeyCodes::UIO_VC_SHIFT_R))
      break;
    pEvent->DataWheel()->Rotation() < 0
        ? gs_windowcv_mouse_wheel_count.fetch_add(1)
        : gs_windowcv_mouse_wheel_count.fetch_sub(1);
  } break;
  default:
    break;
  }
}

void WindowCv::OnWindowIdle() {
  do { //!@ OnMouseWheel and adjust size
    if (0 == gs_windowcv_mouse_wheel_count.load())
      break;
    RECT rect = {0};
    GetWindowRect(hwnd_, &rect);
    const long step = gs_windowcv_mouse_wheel_count.load() * 10; //! px
    rect.left += step;
    rect.top += step;
    rect.right -= step;
    rect.bottom -= step;
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_SHOWWINDOW | SWP_DRAWFRAME);
    gs_windowcv_mouse_wheel_count.store(0);
  } while (0);
}