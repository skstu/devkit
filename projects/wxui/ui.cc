#include "stdafx.h"

wxIMPLEMENT_APP_NO_MAIN(UIWxApp);

wxBEGIN_EVENT_TABLE(UIWxFrame, wxFrame) EVT_SIZE(UIWxFrame::OnSize)
    EVT_CLOSE(UIWxFrame::OnClose) wxEND_EVENT_TABLE();

UIWxFrame::UIWxFrame(wxWindow *parent, wxWindowID id, const wxString &title,
                     const wxPoint &pos /*= wxDefaultPosition*/,
                     const wxSize &size /*= wxDefaultSize*/,
                     long style /*= wxDEFAULT_FRAME_STYLE*/,
                     const wxString &name /*= wxASCII_STR(wxFrameNameStr)*/)
    : wxFrame(parent, id, title, pos, size, style, name) {
}

UIWxFrame::~UIWxFrame() {
}

void UIWxFrame::OnSize(wxSizeEvent &wxEvent) {

  wxEvent.Skip();
}
void UIWxFrame::OnClose(wxCloseEvent &wxEvent) {
  int res = wxMessageBox("Are you sure you want to exit system?", "tip",
                         wxYES_NO, this);
  if (res != wxYES) {
    wxEvent.Veto();
  } else {
    auto handle = wxApp::GetInstance();
    if (handle) {
      wxQueueEvent(handle,
                   new wxThreadEvent(wxEVT_THREAD, WX_CMD_ONAPPDESTORY));
    }
    wxEvent.Skip();
  }
}
/*
WXLRESULT
UIWxFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
  switch (nMsg) {
  case WM_CLOSE: {
  } break;
  case WM_SHOWWINDOW: {
  } break;
  default:
    break;
  }
  return UIWxFrame::MSWWindowProc(nMsg, wParam, lParam);
}*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UIWxApp::UIWxApp() {
}
UIWxApp::~UIWxApp() {
}
bool UIWxApp::OnInit() {
  bool result = false;
  do {
    if (!wxApp::OnInit()) {
      break;
    }
    m_pFrame = new UIWxFrame(nullptr, wxID_ANY, "Hello");
    m_pFrame->Show();
    m_pFrame->Center();
    wxAppBase::SetExitOnFrameDelete(false);
    wxEvtHandler::Bind(wxEVT_THREAD, &UIWxApp::OnFrameShow, this,
                       WX_CMD_ONAPPCREATEFRAME);
    wxEvtHandler::Bind(wxEVT_THREAD, &UIWxApp::OnTerminate, this,
                       WX_CMD_ONAPPDESTORY);

    result = true;
  } while (0);
  return result;
}
int UIWxApp::OnExit() {
  int result = 0;
  do {

    result = wxApp::OnExit();
  } while (0);
  return result;
}
void UIWxApp::OnFrameShow(wxThreadEvent &event) {
  wxInitAllImageHandlers();
}
void UIWxApp::OnTerminate(wxThreadEvent &event) {
  ExitMainLoop();
}

WxMain::WxMain() {
}

WxMain::~WxMain() {
}
void WxMain::Release() const {
  delete this;
}
void WxMain::Center() const {
#if 0
    do {
        auto app = wxDynamicCast(wxApp::GetInstance(), UIWxApp);
        if (!app) {
            break;
        }
        auto frame = app->FrameGet();
        if (!frame) {
            break;
        }
        frame->Center();
    } while (0);
#endif
}
HANDLE gs_wxMainThread = nullptr;
void WxMain::MainThread() {
  do {
    // if (!__gpHinstance) {
    //     break;
    // }
    const HINSTANCE hInstance =
        wxDynamicLibrary::MSWGetModuleHandle("wxui", &gs_wxMainThread);
    wxDISABLE_DEBUG_SUPPORT();
    wxInitializer wxinit;
    if (!wxinit.IsOk()) {
      break;
    }
    auto app = wxDynamicCast(wxApp::GetInstance(), UIWxApp);
    wxEntry(hInstance);
    // wxEntry();
  } while (0);
}

Wxui::Wxui() {
}

Wxui::~Wxui() {
}

bool Wxui::Ready() const {
  return false;
}
void Wxui::Release() const {
  delete this;
}
bool Wxui::UICreate() {
  auto pMain = new WxMain();
  return false;
}
void Wxui::UIDestroy() {
}
Wxui *__gpWxui = nullptr;
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *data, unsigned long len) {
  void *result = nullptr;
  do {
    if (__gpWxui) {
      break;
    }
    __gpWxui = new Wxui();
  } while (0);
  return reinterpret_cast<void *>(dynamic_cast<IWxui *>(__gpWxui));
}
SHARED_API void interface_uninit() {
  do {
    if (!__gpWxui) {
      break;
    }
    delete __gpWxui;
    __gpWxui = nullptr;
  } while (0);
}

#ifdef __cplusplus
}
#endif