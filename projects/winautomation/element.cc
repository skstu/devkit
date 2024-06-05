#include "stdafx.h"

Point::Point() {
}

Point::~Point() {
}

void Point::Release() const {
  delete this;
}