#include "stdafx.h"

ElementArray::ElementArray() {
  elements_.clear();
}
ElementArray::~ElementArray() {
  for (auto &ele : elements_)
    ele->Release();
  elements_.clear();
}
void ElementArray::Release() const {
  delete this;
}
Element *ElementArray::Push(const ELEMENT &element) {
  Element *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  result = new Element(element);
  elements_.push_front(result);
  return result;
}
Element *ElementArray::Push(IUIAutomationElement *pElement) {
  Element *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  result = new Element(pElement);
  elements_.push_front(result);
  return result;
}
IElement *ElementArray::Begin() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (elements_.empty())
    return nullptr;
  return *elements_.begin();
}
IElement *ElementArray::End() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  if (elements_.empty())
    return nullptr;
  return *std::prev(elements_.end());
}

size_t ElementArray::Total() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return elements_.size();
}
IElement *ElementArray::Next(const size_t &index) const {
  IElement *result = nullptr;
  std::lock_guard<std::mutex> lock{*mutex_};
  do {
    if (elements_.empty())
      break;
    if (index >= elements_.size())
      break;
    auto it = elements_.begin();
    std::advance(it, index);
    if (it == elements_.end())
      break;
    result = *it;
  } while (0);
  return result;
}
void ElementArray::Fixed(IUIAutomationElement *pElementDesktop) {
  do {
    if (elements_.empty() || !pElementDesktop)
      break;
    Element *pElement = new Element(pElementDesktop);
    Element *root = *elements_.begin();
    root->SetRoot(pElement);
    root->SetParent(pElement);

    Element *parent = root;
    for (size_t i = 1; i < elements_.size(); ++i) {
      auto it = elements_.begin();
      std::advance(it, i);
      (*it)->SetRoot(root);
      (*it)->SetParent(parent);
      parent = *it;
    }

    pElement->Release();
  } while (0);

#if 0
  auto total = elements_.size();
  int pos = 0;
  for (auto &ele : elements_) {
    ++pos;
    std::cout << pos << " curr==>" << dynamic_cast<IElement *>(ele)->Identify()
              << std::endl;
    std::cout << pos << " root==>" << dynamic_cast<IElement *>(ele)->Root()
              << std::endl;
    std::cout << pos << " parent==>" << dynamic_cast<IElement *>(ele)->Parent()
              << std::endl;
  }
#endif
}
