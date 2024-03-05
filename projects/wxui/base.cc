#include "stdafx.h"

UIBase::UIBase() {
  Init();
}

UIBase::~UIBase() {
  UnInit();
}

void UIBase::Init() {
  threads_.emplace_back([this]() { MainThread(); });
}

void UIBase::UnInit() {
  for (auto &t : threads_) {
    t.join();
  }
  threads_.clear();
}

void UIBase::MainThread() {
  return;
}
