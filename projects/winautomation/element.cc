#include "stdafx.h"
////////////////////////////////////////////////////////////////////////////////////////////////
Point::Point() {
}
Point::Point(const PixelType &_x, const PixelType &_y) : x(_x), y(_y) {
}

Point::~Point() {
}
void Point::operator=(const POINT &pt) {
  x = pt.x;
  y = pt.y;
}
void Point::operator=(const Point &obj) {
  x = obj.x;
  y = obj.y;
}
bool Point::operator==(const POINT &pt) const {
  bool result = false;
  do {
    if (x != pt.x)
      break;
    if (y != pt.y)
      break;
    result = true;
  } while (0);
  return result;
}
bool Point::operator==(const Point &obj) const {
  bool result = false;
  do {
    if (x != obj.x)
      break;
    if (y != obj.y)
      break;
    result = true;
  } while (0);
  return result;
}
void Point::Release() const {
  delete this;
}
const PixelType &Point::GetX() const {
  return x;
}
const PixelType &Point::GetY() const {
  return y;
}
////////////////////////////////////////////////////////////////////////////////////////////////
Rect::Rect() {
}
Rect::Rect(const RECT &rect)
    : left(rect.left), top(rect.top), right(rect.right), bottom(rect.bottom) {
}
Rect::Rect(const PixelType &_l, const PixelType &_t, const PixelType &_r,
           const PixelType &_b)
    : left(_l), top(_t), right(_r), bottom(_b) {
}
Rect::~Rect() {
}
void Rect::operator=(const RECT &rect) {
  left = rect.left;
  top = rect.top;
  right = rect.right;
  bottom = rect.bottom;
}
void Rect::operator=(const Rect &obj) {
  left = obj.left;
  top = obj.top;
  right = obj.right;
  bottom = obj.bottom;
}
void Rect::Release() const {
  delete this;
}
const PixelType &Rect::Left() const {
  return left;
}
const PixelType &Rect::Top() const {
  return top;
}
const PixelType &Rect::Right() const {
  return right;
}
const PixelType &Rect::Bottom() const {
  return bottom;
}
PixelType Rect::GetWidth() const {
  return right - left;
}
PixelType Rect::GetHeight() const {
  return bottom - top;
}
////////////////////////////////////////////////////////////////////////////////////////////////
Position::Position() {
  point_ = new Point();
  rect_ = new Rect();
}

Position::~Position() {
  SK_DELETE_PTR(rect_);
  SK_DELETE_PTR(point_);
}

void Position::Release() const {
  delete this;
}
void Position::operator<<(const RECT &rect) {
  *rect_ = rect;
}
void Position::operator<<(const POINT &pt) {
  *point_ = pt;
}
const IPoint *Position::GetPoint() const {
  return point_;
}
const IRect *Position::GetRect() const {
  return rect_;
}
////////////////////////////////////////////////////////////////////////////////////////////////
Element::Element() {
  Init();
}

Element::~Element() {
  UnInit();
}
void Element::Init() {
  position_ = new Position();
  caprute_point_ = new Point();
}
void Element::UnInit() {
  SK_DELETE_PTR(caprute_point_);
  SK_DELETE_PTR(position_);
}
void Element::Release() const {
  delete this;
}
const IPoint *Element::GetCaprutePoint() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IPoint *>(caprute_point_);
}
const IPosition *Element::GetPosition() const {
  std::lock_guard<std::mutex> lock{*mutex_};
  return dynamic_cast<IPosition *>(position_);
}
void Element::SetCaprutePoint(const POINT &pt) {
  std::lock_guard<std::mutex> lock{*mutex_};
  *caprute_point_ = pt;
}
void Element::operator<<(const RECT &rect) {
  std::lock_guard<std::mutex> lock{*mutex_};
  *position_ << rect;
}

void Element::operator<<(const POINT &pt) {
  std::lock_guard<std::mutex> lock{*mutex_};
  *position_ << pt;
}