#ifndef __PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_H_
#define __PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_H_

class Automation final : public IAutomation,
                         public IUIAutomationFocusChangedEventHandler {
public:
  Automation();
  virtual ~Automation();

private:
  void Init();
  void UnInit();

protected:
  bool Start() override final;
  void Stop() override final;
  bool Ready() const override final;
  void Release() const override final;
  void RegisterCaptureFinishedCb(const unsigned long long &,
                                 const tfCaptureFinishedCb &) override final;

private:
  std::vector<std::thread> threads_;
  std::atomic_bool open_ = false;
  std::atomic_bool ready_ = false;
  std::atomic_bool coinit_ = false;
  tfCaptureFinishedCb capture_finished_cb_ = nullptr;
  void WorkerProc();
  void CallbackProc();
  void OnCaptureFinished(const Element *) const;
  const unsigned int thread_loop_interval_worker_ = 25;   // ms
  const unsigned int thread_loop_interval_callback_ = 25; // ms
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  std::map<unsigned long long, tfCaptureFinishedCb> capture_finished_cbs_;
  IUIAutomation *pAutomation_ = nullptr;
  stl::container::queue<Element> queue_uiautomation_element_;

public:
  static void FillElement(IUIAutomationElement *pElement, Element &element);

protected:
  ULONG STDMETHODCALLTYPE AddRef() override final;
  ULONG STDMETHODCALLTYPE Release() override final;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override final;
  HRESULT STDMETHODCALLTYPE
  HandleFocusChangedEvent(IUIAutomationElement *sender) override final;

private:
  ULONG refCount = 1;
};

#ifdef __cplusplus
extern "C" {
#endif
SHARED_API void *interface_init(void *, unsigned long);
SHARED_API void interface_uninit();
#ifdef __cplusplus
}
#endif
extern Automation *__gpAutomation;

#endif ///__PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_H_