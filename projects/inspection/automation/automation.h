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
  bool Setup(const char *) override final;
  IAutomation::IConfig *ConfigGet() const override final;
  bool Ready() const override final;
  void FreePtr(void **) const override final;
  void Release() const override final;
  void OnMouseMove() override final;
  void OnMouseClicked() override final;
  void RegisterCaptureFinishedCb(const unsigned long long &,
                                 const tfCaptureFinishedCb &) override final;
  IElement *GetElementFromHwnd(const void *hwnd) const override final;
  IElement *FindElementFromElement(
      const IElement *,
      const bool &bBringWindowToTopOnRoot) const override final;
  IElement *
  FindElementFormRoot(const IElement *,
                      const bool &bBringWindowToTopOnRoot) const override final;
  IElement *FindElementFormDesktop(const IElement *) const override final;
  IElement *GetElementLast() const override final;
  IElementArray *GetElementCapture() const override final;
  IElement *CreateElementFromBin(const char *,
                                 const size_t &) const override final;
  IElement *CreateElementFromJson(const char *,
                                  const size_t &) const override final;

private:
  //!@ Desktop
  bool __Refresh() const;
  IUIAutomationCondition *__CreateCondition(const Element *,
                                            const ElementFindCondType &) const;
  bool __ConvertToVariant(const wchar_t *src, VARIANT &dst) const;
  IUIAutomationElement *__GetElementRoot(IUIAutomationElement *) const;
  Element *__GetElementRoot(const Element *) const;
  bool __GetElementByFindAll(const Element *, IUIAutomationElement **) const;
  bool __GetElementByPatch(const Element *, Element **) const;
  void __EnumerateElement(
      IUIAutomationTreeWalker *pTreeWalker, IUIAutomationElement *pElement,
      const std::function<void(IUIAutomationElement *, bool &)> &) const;
  void __EnumerateCacheElement(IUIAutomationElement *pCacheElement,
                               std::vector<IUIAutomationElement *> &all) const;
  bool __DingHandle();
  ElementArray *__GetElementChain(IUIAutomationElement *) const;
  Element *__CreateElement(IUIAutomationElement *) const;
  bool __BuildUpdatedCache(const HWND &);

private:
  win::IWin *pWin_ = nullptr;
  std::atomic_bool is_destroy_win_dll_ = false;
  std::vector<std::thread> threads_;
  std::atomic_bool open_ = false;
  std::atomic_bool ready_ = false;
  std::atomic_bool coinit_ = false;
  tfCaptureFinishedCb capture_finished_cb_ = nullptr;
  void WorkerProc();
  void CallbackProc();
  void RefreshProc();
  void MouseMoveProc();
  void BuildCacheProc();
  const unsigned int thread_loop_interval_worker_ = 10;   // ms
  const unsigned int thread_loop_interval_callback_ = 10; // ms
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
  stl::container::map<HWND, ElementArray *> map_hwnd_build_cache_;
  std::map<unsigned long long, tfCaptureFinishedCb> capture_finished_cbs_;
  Config *config_ = nullptr;
  IUIAutomationElement *pUIElementLast_ = nullptr;
  stl::container::queue<IUIAutomationElement *> queue_uiautomation_element_;
  stl::container::queue<HWND> queue_build_cache_;
  std::map<std::string, Element *> map_element_data_;

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