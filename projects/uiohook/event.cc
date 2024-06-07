#include "stdafx.h"

Event::Event() {
}

Event::~Event() {
}
void Event::Release() const {
  delete this;
}