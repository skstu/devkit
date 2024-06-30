#include "stdafx.h"

Range::Range() {

}
Range::Range(const long& x, const long& y, const long& cx, const long& cy) : x_(x),y_(y),cx_(cx),cy_(cy) {

}
Range::~Range() {

}
void Range::Release() const {
 delete this;
}

long Range::X() const {
 return x_;
}
long Range::Y() const {
 return y_;
}
long Range::CX() const {
 return cx_;
}
long Range::CY() const {
 return cy_;
}