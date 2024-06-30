#include "stdafx.h"

ElementObject::ElementObject() {

}

ElementObject::~ElementObject() {
	SK_RELEASE_PTR(wnd_info_);
}
void ElementObject::Release() const {
	delete this;
}
std::string ElementObject::Key() const {
	if (element_)
		return element_->Identify();
	return "";
}
void ElementObject::Wininfo(win::IWindowInfo* info) {
	wnd_info_ = info;
}
const win::IWindowInfo* ElementObject::Wininfo() const {
	return wnd_info_;
}
void ElementObject::Prownd(const std::string& wnd) {
	pro_wnd_ = wnd;
}
void ElementObject::Elepng(const std::string& png) {
	ele_png_ = png;
}
void ElementObject::Element(inspect::IElement* pElement) {
	element_ = pElement;
}
const inspect::IElement* ElementObject::Element() const {
	return element_;
}
const std::string& ElementObject::Prownd() const {
	return pro_wnd_;
}
const std::string& ElementObject::Elepng() const {
	return ele_png_;
}

bool ElementObject::operator==(const ElementObject& obj) const {
	return stricmp(element_->Identify(), obj.element_->Identify()) == 0;
}
bool ElementObject::operator!=(const ElementObject& obj) const {
	return !(*this == obj);
}
bool ElementObject::operator>(const ElementObject& obj) const {
	return stricmp(element_->Identify(), obj.element_->Identify()) > 0;
}
bool ElementObject::operator<(const ElementObject& obj) const {
	return stricmp(element_->Identify(), obj.element_->Identify()) < 0;
}
void ElementObject::SetWindowType(const WindowType& type) {
	window_type_ = type;
}
const WindowType& ElementObject::GetWindowType() const {
	return window_type_;
}