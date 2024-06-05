#include "stdafx.h"

static ULONG_PTR gdiplustoken_ = 0;
static Gdiplus::GdiplusStartupInput gdiplusStartupInput_ = {0};
static const wchar_t CLASS_NAME[] = L"OverlayWindowClassBySkstu";

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                       LPARAM lParam) {
  switch (uMsg) {
  case WM_CREATE: {
    return 0;
  } break;
  case WM_PAINT: {
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
    Gdiplus::Pen pen(Gdiplus::Color(255, 255, 215, 0), 2);
    // 绘制矩形边框
    int left = rect.left;
    int top = rect.top;
    int width = rect.right - rect.left - 1;
    int height = rect.bottom - rect.top - 1;
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

#if 0

#include "stdafx.h"

void CaptureScreenshot(RECT rect, const wchar_t *filePath) {
  // 初始化 GDI+
  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  // 获取屏幕设备上下文
  HDC hScreenDC = GetDC(NULL);
  HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

  // 创建兼容的位图
  HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, rect.right - rect.left,
                                           rect.bottom - rect.top);
  HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

  // 拷贝屏幕内容到内存设备上下文
  BitBlt(hMemoryDC, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
         hScreenDC, rect.left, rect.top, SRCCOPY);

  // 创建 GDI+ 位图
  Gdiplus::Bitmap bitmap(hBitmap, NULL);

  // 保存位图到文件
  CLSID clsid;
  CLSIDFromString(L"{557CF400-1A04-11D3-9A73-0000F81EF32E}",
                  &clsid); // PNG 编码器 CLSID
  bitmap.Save(filePath, &clsid, NULL);

  // 清理
  SelectObject(hMemoryDC, hOldBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hMemoryDC);
  ReleaseDC(NULL, hScreenDC);
  Gdiplus::GdiplusShutdown(gdiplusToken);
}

#if 0
int main() {
    // 获取 IUIAutomationElement 的 RECT
    RECT boundingRect;
    // 假设已经获取到 pElement
    // pElement->get_CurrentBoundingRectangle(&boundingRect);

    // 示例：捕获整个屏幕
    boundingRect.left = 0;
    boundingRect.top = 0;
    boundingRect.right = GetSystemMetrics(SM_CXSCREEN);
    boundingRect.bottom = GetSystemMetrics(SM_CYSCREEN);

    // 捕获并保存图片
    CaptureScreenshot(boundingRect, L"screenshot.png");

    return 0;
}
#endif
Overlay::Overlay() {
  Init();
}

Overlay::~Overlay() {
  UnInit();
}
void Overlay::Release() const {
  delete this;
}
void Overlay::Init() {
  do {
    Gdiplus::Status status =
        Gdiplus::GdiplusStartup(&gdiplustoken_, &gdiplusStartupInput_, NULL);
    if (Gdiplus::Ok != status)
      break;
    ready_.store(true);
  } while (0);
}
void Overlay::UnInit() {
  do {
    if (!ready_.load())
      break;
    Gdiplus::GdiplusShutdown(gdiplustoken_);
    ready_.store(false);
  } while (0);
}
bool Overlay::Create() {
  do {
    if (open_.load() || !ready_.load())
      break;
    open_.store(true);
    threads_.emplace_back([this]() { WindowProc(); });
  } while (0);
  return open_.load();
}
void Overlay::Destory() {
  do {
    if (!open_.load())
      break;
    PostMessage(hwnd_, WM_CLOSE, 0, 0);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
    open_.store(false);
  } while (0);
}
void Overlay::Update(const RECT &rect) const {
  do {
    if (!hwnd_)
      break;
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, NULL, TRUE);
  } while (0);
}


void Overlay::WindowProc() {
  do {
    const wchar_t CLASS_NAME[] = L"OverlayWindowClassBySkstu";
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
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (1);
  } while (0);
}
#endif