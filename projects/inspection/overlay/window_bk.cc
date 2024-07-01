#include "stdafx.h"
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam) {
	static WindowBk* _this = reinterpret_cast<WindowBk*>(lParam);

	static int mouseX = 0;
	static int mouseY = 0;
	static bool trackingMouse = false;
	switch (uMsg) {
	case WM_CREATE: {
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

		Gdiplus::Pen pen(Gdiplus::Color(0, 255, 215, 0), 2);
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

WindowBk::WindowBk()
	: IWindow(OverlayWindowType::OVERLAY_WINDOW_BK, L"OverlayWindowClassFromBk",
		WS_POPUP,
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
		WS_EX_TOOLWINDOW,
		WindowProc) {
	HWND hDesktop = GetDesktopWindow();
	RECT rtDesktopWindow = { 0 };
	GetClientRect(hDesktop, &rtDesktopWindow);
	default_window_point_.x = rtDesktopWindow.left;
	default_window_point_.y = rtDesktopWindow.top;
	default_window_size_.cx = rtDesktopWindow.right - rtDesktopWindow.left;
	default_window_size_.cy = rtDesktopWindow.bottom - rtDesktopWindow.top;
}

WindowBk::~WindowBk() {
}
void WindowBk::Release() const {
	delete this;
}
void WindowBk::OnCreateWindowSuccess() const {
	SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 0, LWA_COLORKEY);

	SetWindowPos(hwnd_, HWND_TOPMOST,
		default_window_point_.x,
		default_window_point_.y,
		static_cast<int>(default_window_size_.cx * dpiScale),
		static_cast<int>(default_window_size_.cy * dpiScale), SWP_SHOWWINDOW);
	InvalidateRect(hwnd_, NULL, TRUE);
}
void WindowBk::OnUioEvent(const uiohook::IUioEvent* pEvent) const {
	switch (pEvent->Type()) {
	case uiohook::UioEventType::UIO_EVENT_MOUSE_RELEASED: {
	} break;
	default:
		break;
	}
}
bool WindowBk::Screenshot() const {
	bool result = false;
	std::vector<char> imgBuffer;
	imgBuffer.clear();
	IStream* pStream = nullptr;
	HBITMAP hBitmap = nullptr;
	HDC hScreenDC = nullptr;
	HDC hMemoryDC = nullptr;
	do {
		RECT rect;
		GetWindowRect(GetDesktopWindow(), &rect);
		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;
		width = static_cast<int>(width * dpiScale);
		height = static_cast<int>(height * dpiScale);

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
		STATSTG statstg = { 0 };
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
		stl::File::WriteFile(R"(C:\Users\k34ub\Desktop\src.png)",
			std::string(&imgBuffer[0], imgBuffer.size()));
#else
		std::string path = sk::GetCurrentPath();
		if (!stl::File::WriteFile(fmt::format("{}\\{}.png", path, "src"),
			std::string(&imgBuffer[0], imgBuffer.size())))
			break;
#endif
		std::cout << "Screenshot success." << std::endl;
	} while (0);
	return result;
}