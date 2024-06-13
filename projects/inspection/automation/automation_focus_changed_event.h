#ifndef __PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_EVENT_FOCUS_CHANGED_H_
#define __PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_EVENT_FOCUS_CHANGED_H_

class AutomationFocusChangedEvent
    : public IUIAutomationFocusChangedEventHandler {
public:
  AutomationFocusChangedEvent();
  virtual ~AutomationFocusChangedEvent();
  ULONG STDMETHODCALLTYPE AddRef();
  ULONG STDMETHODCALLTYPE Release();
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
  HRESULT STDMETHODCALLTYPE
  HandleFocusChangedEvent(IUIAutomationElement *sender);

private:
  ULONG refCount;
};

#endif ///__PROJECTS_INSPECTION_AUTOMATION_AUTOMATION_EVENT_FOCUS_CHANGED_H_