#include "stdafx.h"
using namespace std;

string Time::CurrentDateA() {
  time_t t = time(nullptr);
  tm *tm = localtime(&t);
  char buffer[64] = {0};
  size_t buffer_len = strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm);
  return string(buffer, buffer_len);
}

wstring Time::CurrentDateW() {
  time_t t = time(nullptr);
  tm *tm = localtime(&t);
  wchar_t buffer[64] = {0};
  size_t buffer_len = wcsftime(buffer, sizeof(buffer), L"%Y-%m-%d", tm);
  return wstring(buffer, buffer_len);
}