#include "stdafx.h"

IWindow::IWindow(const OverlayWindowType &type, const std::wstring &class_name,
                 const unsigned long &style, const unsigned long &exstyle,
                 const WNDPROC &wndproc)
    : type_(type), class_name_(class_name), style_(style), exstyle_(exstyle),
      wndproc_(wndproc) {
  Init();
}

IWindow::~IWindow() {
  UnInit();
}

void IWindow::Init() {
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  handle_ = GetModuleHandleW(NULL);
  GetWindowRect(GetDesktopWindow(), &desktop_rect);
}

void IWindow::UnInit() {
}

bool IWindow::Create() {
  do {
    if (open_.load())
      break;

    open_.store(true);
    threads_.emplace_back([this]() { WindowThread(); });
  } while (0);
  return open_.load();
}
void IWindow::Show(const bool &flag) const {
  do {
    if (!hwnd_)
      break;
    ShowWindow(hwnd_, flag ? SW_SHOW : SW_HIDE);
  } while (0);
}
void IWindow::Destroy() {
  do {
    if (!open_.load())
      break;

    open_.store(false);
    for (auto &t : threads_)
      t.join();
    threads_.clear();
  } while (0);
}
void IWindow::OnDpiInit() {
  HDC hdc = nullptr;
  do {
    if (!hwnd_)
      break;
    if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)))
      break;
    /*if (SUCCEED(GetDpiForWindow(hWnd, &dpiX, &dpiY)))
      break;*/
    hdc = GetDC(hwnd_);
    int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    dpiScale = dpi_x / 96.0f;
  } while (0);
  if (hdc)
    ReleaseDC(hwnd_, hdc);
}
void IWindow::OnWindowIdle() {
}
void IWindow::OnCreateWindowSuccess() const {
}
const OverlayWindowType &IWindow::Type() const {
  return type_;
}

bool IWindow::Screenshot() const {
  return false;
}
void IWindow::OnUioEvent(const uiohook::IUioEvent *pEvent) const {
}
void IWindow::SetPosition(const long &x, const long &y, const long &cx,
                          const long &cy) const {
}
void IWindow::WindowThread() {
  do {
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndproc_;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = handle_;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = class_name_.c_str();
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassExW(&wcex);

    hwnd_ = CreateWindowExW(exstyle_, class_name_.c_str(), NULL, style_,
                            default_window_point_.x, default_window_point_.y,
                            default_window_size_.cx, default_window_size_.cy,
                            NULL, NULL, handle_, this);

    if (!hwnd_)
      break;

    // SetWindowLongPtrW(hwnd_, GWLP_USERDATA,
    // reinterpret_cast<LONG_PTR>(this));

    /*if (FALSE ==
     SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY))
     break;*/

    OnDpiInit();
    OnCreateWindowSuccess();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

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
          DispatchMessageW(&msg);
        }
      }
      if (!open_.load()) {
        SendMessageW(hwnd_, WM_DESTROY, 0, 0);
        break;
      } else {
        OnWindowIdle();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (1);

  } while (0);
}