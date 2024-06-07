#include "stdafx.h"

static ULONG_PTR gdiplustoken_ = 0;
static Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = {0};
static const wchar_t CLASS_NAME[] = L"OverlayWindowClassBySkstu";

static bool __Screenshot(const HWND &hwnd, char **buffer, size_t *len) {
  bool result = false;
  *buffer = nullptr;
  *len = 0;
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
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &pStream)))
      break;
    CLSID clsid;
    if (FAILED(
            CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}", &clsid)))
      break;
    if (Gdiplus::Status::Ok != bitmap.Save(pStream, &clsid, NULL))
      break;
    if (!pStream)
      break;
    STATSTG statstg = {0};
    if (FAILED(pStream->Stat(&statstg, STATFLAG_NONAME)))
      break;
    ULONG streamSize = statstg.cbSize.LowPart;
    *len = streamSize;
    *buffer = (char *)malloc(*len);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
    ULONG bytesRead = 0;
    pStream->Read(buffer, streamSize, &bytesRead);
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
  if (!result) {
    if (*buffer) {
      free(*buffer);
      *buffer = nullptr;
    }
    *len = 0;
  }
  return result;
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                       LPARAM lParam) {

  LONG_PTR theClass = GetWindowLongPtrW(hwnd, GWLP_USERDATA);

  switch (uMsg) {
  case WM_CREATE: {
    return 0;
  } break;
  case WM_PAINT: {
    auto _this = reinterpret_cast<Overlay *>(theClass);
    if (!_this)
      break;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rect;
    GetClientRect(hwnd, &rect);
#if 0
    HPEN hPenRed = CreatePen(PS_SOLID, 2, RGB(255, 215, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenRed);
    rect.right -= 1;
    rect.bottom -= 1;
    MoveToEx(hdc, rect.left, rect.top, NULL);
    LineTo(hdc, rect.right, rect.top);
    LineTo(hdc, rect.right, rect.bottom);
    LineTo(hdc, rect.left, rect.bottom);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPenRed);
#else
    // 创建Graphics对象
    Gdiplus::Graphics graphics(hdc);
    // 创建一个红色的画笔
    // Gdiplus::Pen pen(Gdiplus::Color(255, 255, 215, 0), 2);
    Gdiplus::Pen pen(Gdiplus::Color(255, 0, 122, 204), 3);
    // 绘制矩形边框
    int left = rect.left;
    int top = rect.top;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    // 调用明确的DrawRectangle重载函数
    graphics.DrawRectangle(&pen, left, top, width, height);
#endif
    /*SetTextColor(hdc, RGB(255, 255, 255)); // 白色字体
    SetBkMode(hdc, TRANSPARENT);           // 透明背景
    HFONT hFont =
        CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"Arial");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    const wchar_t *text = L"Overlay Text";
    RECT textRect = rect;
    textRect.left = textRect.right - 150; // 调整文本位置
    textRect.top = textRect.top + 10;
    DrawTextW(hdc, text, -1, &textRect, DT_SINGLELINE | DT_NOCLIP);
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);*/

    EndPaint(hwnd, &ps);
  } break;
  case WM_ERASEBKGND: {
    return FALSE;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    std::cout << "Overlay module exit." << std::endl;
    break;
  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

Overlay::Overlay() {
  Init();
}
Overlay::~Overlay() {
  UnInit();
}
void Overlay::Release() const {
  delete this;
}
bool Overlay::Ready() const {
  return open_.load();
}
void Overlay::Init() {
  Gdiplus::GdiplusStartup(&gdiplustoken_, &gdiplusStartupInput_, NULL);
}
void Overlay::UnInit() {
  Gdiplus::GdiplusShutdown(gdiplustoken_);
}
bool Overlay::Start() {
  do {
    if (open_.load())
      break;
    threads_.emplace_back([this]() { WindowThread(); });
    open_.store(true);
  } while (0);
  return open_.load();
}
void Overlay::Stop() {
  do {
    if (!open_.load())
      break;
    open_.store(false);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
bool Overlay::Screenshot(char **buffer, size_t *len) const {
  bool result = false;
  *buffer = nullptr;
  *len = 0;
  do {
    if (!__Screenshot(hwnd_, buffer, len))
      break;
    result = true;
  } while (0);
  return result;
}
void Overlay::Free(void **p) const {
  do {
    if (!(*p))
      break;
    free(*p);
    *p = nullptr;
  } while (0);
}
void Overlay::AppendPos(const long &x, const long &y, const long &width,
                        const long &height) {
  RECT pos = {0};
  pos.left = x;
  pos.top = y;
  pos.right = x + width;
  pos.bottom = y + height;
  pos.right -= 1;
  pos.bottom -= 1;
  posq_.push(pos);
}
void Overlay::WindowThread() {
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

    /*HWND desk = GetDesktopWindow();
    RECT rtDesk;
    GetWindowRect(desk, &rtDesk);
    Update(rtDesk);*/
    MSG msg = {0};
    do {
      if (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE) != 0) {
        if (msg.message == WM_QUERYENDSESSION || msg.message == WM_CLOSE ||
            msg.message == WM_DESTROY || msg.message == WM_QUIT ||
            msg.message == WM_ENDSESSION) {
          SendMessageW(hwnd_, WM_DESTROY, 0, 0);
          break;
        } else {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
      }
      if (!open_.load()) {
        SendMessageW(hwnd_, WM_DESTROY, 0, 0);
        break;
      } else {
        auto poss = posq_.pops();
        for (const auto &pos : poss) {
          SetWindowPos(hwnd_, HWND_TOPMOST, pos.left, pos.top,
                       pos.right - pos.left, pos.bottom - pos.top,
                       SWP_SHOWWINDOW);
          InvalidateRect(hwnd_, NULL, TRUE);
        }
        /*if (poss.empty()) {
          InvalidateRect(hwnd_, NULL, TRUE);
        }*/
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (1);
  } while (0);
}
Overlay *__gpOverlay = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpOverlay) {
      break;
    }
    __gpOverlay = new Overlay();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IOverlay *>(__gpOverlay));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpOverlay) {
      break;
    }
    delete __gpOverlay;
    __gpOverlay = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif
