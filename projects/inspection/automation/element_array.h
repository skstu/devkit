#ifndef __PROJECTS_INSPECTION_AUTOMATION_ELEMENT_ARRAY_H_
#define __PROJECTS_INSPECTION_AUTOMATION_ELEMENT_ARRAY_H_

class ElementArray final : public IElementArray {
public:
  ElementArray();
  virtual ~ElementArray();

public:
  IElement *Begin() const override final;
  IElement *End() const override final;
  void Release() const override final;
  IElement *Next(const size_t &) const override final;

public:
  size_t Total() const override final;
  Element *Push(const tagElement &);
  Element *Push(IUIAutomationElement *);
  void Fixed(IUIAutomationElement *);

private:
  std::list<Element *> elements_;
  std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

#endif //__PROJECTS_INSPECTION_AUTOMATION_ELEMENT_ARRAY_H_