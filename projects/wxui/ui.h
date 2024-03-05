#ifndef __PROJECTS_WXUI_UI_H_
#define __PROJECTS_WXUI_UI_H_

const int WX_CMD_ONAPPCREATEFRAME = wxNewId();
const int WX_CMD_ONAPPDESTORY = wxNewId();
const int WX_CMD_SHOWWINDOW = wxNewId();
const int WX_CMD_CreateMDIChildFrame = wxNewId();

class UIWxFrame : public wxFrame {
public:
  UIWxFrame(wxWindow *parent, wxWindowID id, const wxString &title,
            const wxPoint &pos = wxDefaultPosition,
            const wxSize &size = wxDefaultSize,
            long style = /*wxDEFAULT_FRAME_STYLE*/ wxNO_BORDER | wxFRAME_SHAPED,
            const wxString &name = wxASCII_STR(wxFrameNameStr));
  virtual ~UIWxFrame();

private:
  wxToolBar *m_pToolbarMain = nullptr;
  wxMenuBar *m_pMenuMain = nullptr;
  wxStatusBar *m_pStatusbarMain = nullptr;

private:
  void OnSize(wxSizeEvent &);
  void OnClose(wxCloseEvent &);
  /*WXLRESULT
  MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override final;*/
  wxDECLARE_EVENT_TABLE();
};

class UIWxApp : public wxApp {
public:
  UIWxApp();
  virtual ~UIWxApp();

private:
  UIWxFrame *m_pFrame = nullptr;

public:
  bool OnInit() override;
  int OnExit() override;

private:
  void OnFrameShow(wxThreadEvent &event);
  void OnTerminate(wxThreadEvent &event);

protected:
  std::vector<std::thread> m_Threads;
};

class WxMain final : UIBase {
  std::shared_ptr<std::mutex> m_Mutex = std::make_shared<std::mutex>();

public:
  WxMain();

protected:
  void MainThread();
  void Release() const;
  void Center() const;

private:
  ~WxMain();
};

class Wxui final : public IWxui {
public:
  Wxui();
  virtual ~Wxui();

protected:
  bool Ready() const override final;
  void Release() const override final;
  bool UICreate() override final;
  void UIDestroy() override final;
};
#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Wxui *__gpWxui;
#endif ///__PROJECTS_WXUI_UI_H_
