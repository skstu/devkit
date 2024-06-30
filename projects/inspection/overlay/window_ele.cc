#include "stdafx.h"

static std::atomic_long gs_windowele_mouse_wheel_count = 0;
static std::atomic_bool gs_windowele_screenshot_switch = false;
#if 1
bool __ScreenshotEle(const HWND &hwnd) {
  gs_windowele_screenshot_switch.store(false);
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
bool __ScreenshotEle(const HWND &hwnd) {
  gs_windowele_screenshot_switch.store(false);
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

void WindowEle::SetRegion(const HWND &hWnd) const {
  HRGN hRgn = nullptr;
  RECT rect;
  GetWindowRect(hWnd, &rect);
  int width = rect.right - rect.left;
  int height = rect.bottom - rect.top;
  switch (layered_window_type_) {
  case LayeredWindowType::Circular: {
    hRgn = CreateEllipticRgn(0, 0, width, height);
  } break;
  case LayeredWindowType::Rectangle: {
    hRgn = CreateRectRgn(0, 0, width, height);
  } break;
  default:
    break;
  }
  if (hWnd && hRgn)
    SetWindowRgn(hWnd, hRgn, TRUE);
}

void WindowEle::OnPaint(const HWND &hWnd) const {
  do {
    Config *pConfig = dynamic_cast<Config *>(__gpOverlay->ConfigGet());
    if (!hWnd || !pConfig)
      return;
    const auto IsScreenshot = pConfig->EnableScreenshot();

    RECT rtClient;
    RECT rtWindow;
    GetClientRect(hWnd, &rtClient);
    GetWindowRect(hWnd, &rtWindow);
    int width = rtClient.right - rtClient.left;
    int height = rtClient.bottom - rtClient.top;

    HDC hdc = GetDC(hWnd);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

    // Clear background
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rtClient, hBrush);
    DeleteObject(hBrush);
    do {
      Gdiplus::Graphics graphics(hdcMem);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      // Color color(20, 255, 215, 0);//!@ 土豪金
      Gdiplus::Color color(IsScreenshot ? 0 : 100, 183, 110,
                           121); //!@ 玫瑰金
      Gdiplus::SolidBrush brush(color);

      BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

      switch (layered_window_type_) {
      case LayeredWindowType::Circular: {
        graphics.FillEllipse(&brush, 0, 0, rtClient.right - rtClient.left,
                             rtClient.bottom - rtClient.top);

        POINT ptSrc = {0, 0};
        SIZE sizeWnd = {width, height};
        POINT ptDst;
        GetCursorPos(&ptDst);
        ptDst.x -= (width / 2);
        ptDst.y -= (height / 2);

        UpdateLayeredWindow(hWnd, hdc, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0,
                            &blend, ULW_ALPHA);
      } break;
      case LayeredWindowType::Rectangle: {
        graphics.FillRectangle(
            &brush, Gdiplus::Rect(0, 0, rtClient.right - rtClient.left,
                                  rtClient.bottom - rtClient.top));
        Gdiplus::Pen pen(Gdiplus::Color(IsScreenshot ? 0 : 255, 255, 0, 0), 5);
        // 255, 215, 0
        // Gdiplus::Pen pen(Gdiplus::Color(IsScreenshot ? 0 : 255, 255, 215, 0),
        // 5);
        graphics.DrawRectangle(
            &pen, Gdiplus::Rect(0, 0, rtClient.right - rtClient.left - 1,
                                rtClient.bottom - rtClient.top - 1));
#if 0
      // const unsigned int font_width =
      //     static_cast<unsigned int>((rect.right - rect.left) / 30);
      Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
      Gdiplus::Font font(&fontFamily, 20, Gdiplus::FontStyleRegular,
                         Gdiplus::UnitPixel);
      Gdiplus::SolidBrush textBrush(
          Gdiplus::Color(IsScreenshot ? 0 : 200, 255, 255, 255));
      Gdiplus::SolidBrush backgroundBrush(
          Gdiplus::Color(IsScreenshot ? 0 : 100, 255, 215, 0));
      /*Gdiplus::RectF backgroundRect(0, 0, font_width * wcslen(text) * 1.1,
                                    20 * 1.1);*/
      // 获取文本的大小
      Gdiplus::PointF origin(0, 0);
      Gdiplus::RectF textRect;
      graphics.MeasureString(pConfig->GetLabel().c_str(), -1, &font, origin,
                             &textRect);
      Gdiplus::RectF backgroundRect(0, 0, textRect.Width, textRect.Height);
      graphics.FillRectangle(&backgroundBrush, backgroundRect);
      graphics.DrawString(pConfig->GetLabel().c_str(), -1, &font, origin,
                          &textBrush);
#endif
        POINT ptDst = {rtWindow.left, rtWindow.top};
        // POINT ptDst = {0, 0};
        POINT ptSrc = {0, 0};
        SIZE sizeWnd = {rtClient.right - rtClient.left,
                        rtClient.bottom - rtClient.top};

        UpdateLayeredWindow(hWnd, hdc, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0,
                            &blend, ULW_ALPHA);
      } break;
      default:
        break;
      }
    } while (0);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(hWnd, hdc);
  } while (0);
}
LRESULT CALLBACK WindowEle::WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                       LPARAM lParam) {
  static WindowEle *_this = reinterpret_cast<WindowEle *>(lParam);
  switch (message) {
  case WM_CREATE: {
    return FALSE;
  }
  case WM_SIZE:
  case WM_SIZING:
    if (!_this)
      break;
    _this->SetRegion(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
    break;
  case WM_PAINT: {
    if (!_this)
      break;
    _this->OnPaint(hWnd);
  } break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

WindowEle::WindowEle()
    : IWindow(OverlayWindowType::OVERLAY_WINDOW_ELE,
              L"OverlayWindowClassFromEle", WS_POPUP,
              WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                  WS_EX_TOOLWINDOW,
              WindowProc) {
  Init();
}

WindowEle::~WindowEle() {
  UnInit();
}
void WindowEle::Init() {
  layered_window_type_ = LayeredWindowType::Rectangle;
  default_window_point_.x = 0;
  default_window_point_.y = 0;
  default_window_size_.cx = 0;
  default_window_size_.cy = 0;
}
void WindowEle::UnInit() {
}
void WindowEle::OnCreateWindowSuccess() const {
  SetRegion(hwnd_);
  InvalidateRect(hwnd_, NULL, FALSE);
}
void WindowEle::SetPosition(const long &x, const long &y, const long &cx,
                            const long &cy) const {
  if (cx == desktop_rect.right - desktop_rect.left &&
      cy == desktop_rect.bottom - desktop_rect.top) {
    return;
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, cx, cy,
               SWP_SHOWWINDOW | SWP_DRAWFRAME);
  SetRegion(hwnd_);
  InvalidateRect(hwnd_, NULL, FALSE);
}
bool WindowEle::Screenshot() const {
  bool result = false;
  do {
    if (!hwnd_)
      break;
    if (FALSE == InvalidateRect(hwnd_, NULL, FALSE))
      break;
    if (FALSE == UpdateWindow(hwnd_))
      break;
    gs_windowele_screenshot_switch.store(true);
    SendMessageW(hwnd_, WM_PAINT, 0, 0);
    result = true;
  } while (0);
  return result;
}
void WindowEle::Release() const {
  delete this;
}
void WindowEle::OnUioEvent(const uiohook::IUioEvent *pEvent) const {
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
        ? gs_windowele_mouse_wheel_count.fetch_add(1)
        : gs_windowele_mouse_wheel_count.fetch_sub(1);
  } break;
  default:
    break;
  }
}

void WindowEle::OnWindowIdle() {
  do { //!@ OnMouseWheel and adjust size
    if (0 == gs_windowele_mouse_wheel_count.load())
      break;
    RECT rect = {0};
    GetWindowRect(hwnd_, &rect);
    const long step = gs_windowele_mouse_wheel_count.load() * 10; //! px
    rect.left += step;
    rect.top += step;
    rect.right -= step;
    rect.bottom -= step;
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_SHOWWINDOW | SWP_DRAWFRAME);
    gs_windowele_mouse_wheel_count.store(0);
  } while (0);
}