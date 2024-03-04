#ifndef __PROJECTS_WXUI_WXUI_H_
#define __PROJECTS_WXUI_WXUI_H_

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
  WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam,
                          WXLPARAM lParam) override final;
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

#endif ///__PROJECTS_WXUI_WXUI_H_