#include "stdafx.h"

static int rect_offset_windowsui = 0;

static std::atomic_bool gs_windowui_screenshot_switch = false;

static void OnPaint(const HWND &hWnd, const bool &screenshot = false) {
  HDC hdc = nullptr;
  HDC hdcMem = nullptr;
  HBITMAP hbmMem = nullptr;
  HGDIOBJ hOld = nullptr;

  do {
    if (!hWnd)
      break;
    hdc = GetDC(hWnd);
    if (!hdc)
      break;
    // 使用双缓冲技术绘图
    RECT rect;
    GetClientRect(hWnd, &rect);
    rect.right -= rect_offset_windowsui;
    rect.bottom -= rect_offset_windowsui;
    int left = rect.left;
    int top = rect.top;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem)
      break;
    hbmMem = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    if (!hbmMem)
      break;
    hOld = SelectObject(hdcMem, hbmMem);

    // 清除背景
    Gdiplus::Graphics graphicsMem(hdcMem);
    graphicsMem.Clear(Gdiplus::Color(0, 0, 0, 0)); // 透明背景

    Gdiplus::Pen pen(Gdiplus::Color(screenshot ? 0 : 255, 255, 215, 0), 5);
    graphicsMem.DrawRectangle(&pen, left, top, width, height);

    BitBlt(hdc, 0, 0, rect.right, rect.bottom, hdcMem, 0, 0, SRCCOPY);
  } while (0);

  if (hdcMem && hOld)
    SelectObject(hdcMem, hOld);
  if (hbmMem)
    DeleteObject(hbmMem);
  if (hdcMem)
    DeleteDC(hdcMem);
  if (hWnd && hdc)
    ReleaseDC(hWnd, hdc);
}

static void OnScreenshot(const HWND &hWnd) {
  std::vector<char> imgBuffer;
  imgBuffer.clear();
  IStream *pStream = nullptr;
  HBITMAP hBitmap = nullptr;
  HDC hScreenDC = nullptr;
  HDC hMemoryDC = nullptr;
  do {
    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    hScreenDC = GetDC(NULL);
    hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hScreenDC || !hMemoryDC)
      break;
    hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    if (!hBitmap || !hOldBitmap)
      break;
    if (FALSE == BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, rect.left,
                        rect.top, SRCCOPY))
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
    stl::File::WriteFile(R"(C:\Users\k34ub\Desktop\ctrl.png)",
                         std::string(&imgBuffer[0], imgBuffer.size()));
#else
    std::string path = sk::GetCurrentPath();
    if (!stl::File::WriteFile(fmt::format("{}\\{}.png", path, "ctrl"),
                              std::string(&imgBuffer[0], imgBuffer.size())))
      break;
#endif
    std::cout << "Screenshot success." << std::endl;
  } while (0);
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam) {
  static WindowBk *_this = reinterpret_cast<WindowBk *>(lParam);

  static int mouseX = 0;
  static int mouseY = 0;
  static bool trackingMouse = false;
  switch (uMsg) {
  case WM_CREATE: {
    return FALSE;
  } break;
  case WM_PAINT: {
    // PAINTSTRUCT ps;
    // HDC hdc = BeginPaint(hWnd, &ps);
    // EndPaint(hWnd, &ps);
    if (gs_windowui_screenshot_switch.load()) {
      OnPaint(hWnd, true);
      OnScreenshot(hWnd);
      gs_windowui_screenshot_switch.store(false);
    } else {
      OnPaint(hWnd);
    }
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
WindowUi::WindowUi()
    : IWindow(OverlayWindowType::OVERLAY_WINDOW_UI, L"OverlayWindowClassFromUi",
              WS_POPUP,
              WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                  WS_EX_TOOLWINDOW,
              WindowProc) {
  default_window_point_.x = 0;
  default_window_point_.y = 0;
  default_window_size_.cx = 0;
  default_window_size_.cy = 0;
}

WindowUi::~WindowUi() {
}
void WindowUi::Release() const {
  delete this;
}

void WindowUi::OnCreateWindowSuccess() const {
  SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);

  SetWindowPos(hwnd_, HWND_TOPMOST, default_window_point_.x,
               default_window_point_.y, default_window_size_.cx,
               default_window_size_.cy, SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, NULL, TRUE);
}
void WindowUi::SetPosition(const long &x, const long &y, const long &cx,
                           const long &cy) const {
  if (cx == desktop_rect.right - desktop_rect.left &&
      cy == desktop_rect.bottom - desktop_rect.top) {
    return;
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, cx, cy, SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, NULL, TRUE);
}

bool WindowUi::Screenshot() const {
  bool result = false;
  do {
    if (!hwnd_)
      break;
    if (FALSE == InvalidateRect(hwnd_, NULL, FALSE))
      break;
    if (FALSE == UpdateWindow(hwnd_))
      break;
    gs_windowui_screenshot_switch.store(true);
    SendMessageW(hwnd_, WM_PAINT, 0, 0);
    result = true;
  } while (0);
  return result;
}
