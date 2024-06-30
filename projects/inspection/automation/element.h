#ifndef __PROJECTS_INSPECTION_AUTOMATION_ELEMENT_H_
#define __PROJECTS_INSPECTION_AUTOMATION_ELEMENT_H_

#pragma pack(push, 1)
typedef struct tagElement {
  typedef struct {
    long x;
    long y;
  } Point;
  typedef struct {
    long left;
    long top;
    long right;
    long bottom;
  } Position;
  typedef struct AutomationElement {
    int RuntimeId[32];
    int RuntimeIdLength;
    int ProcessId;
    int ControlType;
    wchar_t LocalizedControlType[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t Name[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AcceleratorKey[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AccessKey[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int HasKeyboardFocus;
    int IsKeyboardFocusable;
    int IsEnabled;
    wchar_t AutomationId[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t ClassName[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t HelpText[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int Culture;
    int IsControlElement;
    int IsContentElement;
    int IsPassword;
    unsigned long long NativeWindowHandle;
    wchar_t ItemType[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int IsOffscreen;
    int Orientation;
    wchar_t FrameworkId[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int IsRequiredForForm;
    wchar_t ItemStatus[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AriaRole[MAX_ELEMENT_ATTRIBUTES_TEXT];
    wchar_t AriaProperties[MAX_ELEMENT_ATTRIBUTES_TEXT];
    int IsDataValidForForm;
    wchar_t ProviderDescription[MAX_ELEMENT_ATTRIBUTES_TEXT]; // BSTR
  } Attributes;

  unsigned long long __begin__;
  Attributes attr;
  Position pos;
  char md5[33];
  char parent[33];
  char root[33];
  char hmac_sha1[41];
  char hmac_sha256[65];
  char runtimeid[129];
  unsigned long long __end__;

  tagElement();
  ~tagElement();
  bool Verify() const;
  void operator=(const tagElement &obj);
  bool operator==(const tagElement &obj) const;
  bool operator!=(const tagElement &obj) const;
  bool operator>(const tagElement &obj) const;
  bool operator<(const tagElement &obj) const;
} ELEMENT, *PELEMENT;
const size_t LEN_ELEMENT = sizeof(ELEMENT);
#pragma pack(pop)

class Element final : public IElement {
public:
  Element();
  Element(const ELEMENT &);
  Element(IUIAutomationElement *);
  void Release() const override final;
  IElement *Clone() const override final;

private:
  virtual ~Element();
  void operator<<(IUIAutomationElement *);
  void operator<<(const ELEMENT &);

public:
  const char *RuntimeId() const override final;
  int ProcessId() const override final;
  int ControlType() const override final;
  const wchar_t *LocalizedControlType() const override final;
  const wchar_t *Name() const override final;
  const wchar_t *AcceleratorKey() const override final;
  const wchar_t *AccessKey() const override final;
  bool HasKeyboardFocus() const override final;
  bool IsKeyboardFocusable() const override final;
  bool IsEnabled() const override final;
  const wchar_t *AutomationId() const override final;
  const wchar_t *ClassName() const override final;
  const wchar_t *HelpText() const override final;
  int Culture() const override final;
  bool IsControlElement() const override final;
  bool IsContentElement() const override final;
  bool IsPassword() const override final;
  unsigned long long NativeWindowHandle() const override final;
  const wchar_t *ItemType() const override final;
  bool IsOffscreen() const override final;
  int Orientation() const override final;
  const wchar_t *FrameworkId() const override final;
  bool IsRequiredForForm() const override final;
  const wchar_t *ItemStatus() const override final;
  const wchar_t *AriaRole() const override final;
  const wchar_t *AriaProperties() const override final;
  bool IsDataValidForForm() const override final;
  const wchar_t *ProviderDescription() const override final;

  long Left() const override final;
  long Top() const override final;
  long Right() const override final;
  long Bottom() const override final;
  long Width() const override final;
  long Height() const override final;
  long CenterX() const override final;
  long CenterY() const override final;

  const char *Parent() const override final;
  const char *Root() const override final;
  const char *Identify() const override final;
  const char *Sha1() const override final;
  const char *Sha256() const override final;
  int Compare(const IElement *) const override final;
  bool ImageStream(char **, size_t *) const override final;
  void Serialization(char **, size_t *) const override final;
  unsigned int Total() const override final;
  void Screenshot() override final;
  bool IsRoot() const override final;
  bool TopMost() const override final;
  bool IsFake() const override final;

public:
  void GetRect(tagRECT &) const;
  void SetParent(IElement *);
  void SetRoot(IElement *);
  void SetRect(const long &left, const long &top, const long &right,
               const long &bottom);
  void Final();
  bool Verify() const override final;
  void Fake(const bool &);

private:
  ELEMENT element_;
  std::string imgBuffer_;
  std::atomic_bool fake_ = false;
};

#endif //__PROJECTS_INSPECTION_AUTOMATION_ELEMENT_H_