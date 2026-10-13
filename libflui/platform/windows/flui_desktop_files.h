#pragma once
#include <windows.h>
#include <shellapi.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <memory>
#include <string>
#include <vector>

// Single-surface host adapter: local filesystem drops, copy semantics only.
class FluiDesktopFiles {
  HWND window_;
  bool enabled_=false;
  flutter::MethodChannel<flutter::EncodableValue> channel_;
public:
  FluiDesktopFiles(HWND window,flutter::BinaryMessenger* messenger)
      :window_(window),channel_(messenger,"devkit.libflui/desktop_files",&flutter::StandardMethodCodec::GetInstance()) {
    channel_.SetMethodCallHandler([this](const auto& call,auto result) {
      if(call.method_name()!="set_enabled"){result->NotImplemented();return;}
      const auto value=call.arguments()?std::get_if<bool>(call.arguments()):nullptr;
      if(!value){result->Error("argument","Expected bool");return;}
      enabled_=*value;DragAcceptFiles(window_,enabled_);result->Success();
    });
  }
  ~FluiDesktopFiles(){DragAcceptFiles(window_,FALSE);channel_.SetMethodCallHandler(nullptr);}
  void Drop(HDROP drop) {
    flutter::EncodableList paths;
    const auto count=DragQueryFileW(drop,0xffffffff,nullptr,0);
    size_t bytes=0;
    if(enabled_ && count<=32)for(UINT i=0;i<count;++i) {
      const auto size=DragQueryFileW(drop,i,nullptr,0);
      if(!size || size>32767){paths.clear();break;}
      std::vector<wchar_t> path(size+1);
      if(DragQueryFileW(drop,i,path.data(),size+1)!=size){paths.clear();break;}
      const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),size,nullptr,0,nullptr,nullptr);
      if(length<=0 || (bytes+=length)>60000){paths.clear();break;}
      std::string utf8(length,'\0');
      WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),size,utf8.data(),length,nullptr,nullptr);
      paths.emplace_back(utf8);
    }
    DragFinish(drop);
    if(enabled_)channel_.InvokeMethod("drop",std::make_unique<flutter::EncodableValue>(paths));
  }
};
