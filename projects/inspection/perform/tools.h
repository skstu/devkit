#ifndef __PROJECTS_INSPECTION_PERFORM_TOOLS_H_
#define __PROJECTS_INSPECTION_PERFORM_TOOLS_H_

class Tools {
public:
  Tools();
  void Release() const;

private:
  ~Tools();
  void Init();
  void UnInit();

public:
  const std::string &CurrentProcessPath() const;
  void GetTaskbarProcesses() const;
  void OnCaptureCanceled() const;
  void OnCaptureFinished(const inspect::IElement *,
                         const inspect::IOverlay::IStream *) const;
  void OnCaptureFinished(const inspect::IElement *,
                         const std::string &imgBuffer) const;
  void OnCaptureFinished(const inspect::IElementArray *) const;
  void TestFindElement() const;

private:
  void GenTaskbarProcessesCapture(
      const std::vector<const inspect::IElement *> &,
      const inspect::IOverlay::IStream *imgData = nullptr) const;
  void GenTaskbarProgramData(const std::string &elekey,
                             const std::string &json) const;

private:
  std::string current_process_path_;
  std::string data_dir_;
  std::string shared_file_capture_;
  std::string shared_file_processes_;
  const std::string file_pro_wnd_ = "pro.wnd";
  const std::string file_ele_bin_ = "ele.bin";
  const std::string file_ele_png_ = "ele.png";
};
extern inspect::IOverlay *gpOverlay;
extern uiohook::IUiohook *gpUiohook;
extern inspect::IAutomation *gpAutomation;
extern Tools *gpTools;
extern win::IWin *gpWin;

#endif ///__PROJECTS_INSPECTION_PERFORM_TOOLS_H_