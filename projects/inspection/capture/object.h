#ifndef CAPTURE_OBJECT_H
#define CAPTURE_OBJECT_H

class ElementObject {
public:
	ElementObject();

	bool operator==(const ElementObject& obj) const;
	bool operator!=(const ElementObject& obj) const;
	bool operator>(const ElementObject& obj) const;
	bool operator<(const ElementObject& obj) const;
private:
	~ElementObject();
public:
	void Prownd(const std::string&);
	void Elepng(const std::string&);
	void Element(inspect::IElement*);
	void Wininfo(win::IWindowInfo*);
	const win::IWindowInfo* Wininfo() const;
	const std::string& Prownd() const;
	const std::string& Elepng() const;
	const inspect::IElement* Element() const;
	void Release() const;
	std::string Key() const;
	void SetWindowType(const WindowType&);
	const WindowType& GetWindowType() const;
private:
	std::string pro_wnd_;
	std::string ele_png_;
	win::IWindowInfo* wnd_info_ = nullptr;
	inspect::IElement* element_ = nullptr;
	WindowType window_type_ = WindowType::WINDOW_TYPE_DESKTOP;
};

#endif //CAPTURE_OBJECT_H
