#include <libsys/device_info.h>
#include "device_form.hpp"
#include "device_text.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#if !TARGET_OS_IPHONE
#include <IOKit/IOKitLib.h>
#endif
#elif defined(__ANDROID__)
#include <sys/system_properties.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace {
template<size_t N> void copy(char (&out)[N], std::string_view value) {
  value=libsys::device::Text(value,N-1);
  if(!value.empty())std::memcpy(out,value.data(),value.size());out[value.size()]=0;
}
#if defined(__linux__) && !defined(__ANDROID__)
std::string read(const char* path) {
  std::ifstream f(path,std::ios::binary);std::string out;
  for(char c;out.size()<127 && f.get(c);)if(static_cast<unsigned char>(c)>=32)out+=c;
  while(!out.empty() && out.back()==' ')out.pop_back();
  return out;
}
#endif
#if defined(__APPLE__)
std::string sysctl_text(const char* name) {
  char b[128]{};size_t n=sizeof(b);if(sysctlbyname(name,b,&n,nullptr,0)!=0)return {};
  b[sizeof(b)-1]=0;return b;
}
#elif defined(__ANDROID__)
std::string property(const char* name) {char b[PROP_VALUE_MAX]{};__system_property_get(name,b);return b;}
#elif defined(_WIN32)
std::string firmware_string(const std::vector<unsigned char>& bytes,size_t start,unsigned index) {
  if(!index)return {};unsigned current=1;std::string out;
  for(size_t i=start;i+1<bytes.size();++i){
    if(bytes[i]==0){if(current==index)return out;if(bytes[i+1]==0)return {}; ++current;out.clear();}
    else if(current==index && out.size()<127 && bytes[i]>=32)out+=static_cast<char>(bytes[i]);
  }return {};
}
#endif
}
extern "C" int dksys_device_detect(dksys_device_info* output) {
  if(!output || output->struct_size<sizeof(*output) || output->version!=1)return -1;
  dksys_device_info info{};info.struct_size=sizeof(info);info.version=1;
  std::string platform="unknown",form="unknown",model,source="unknown";
  try {
#if defined(__APPLE__)
#if TARGET_OS_IPHONE
    model=sysctl_text("hw.machine");platform="ios";
    if(model.starts_with("iPhone") || model.starts_with("iPod")){form="phone";source="model-family";}
    else if(model.starts_with("iPad")){platform="ipados";form="tablet";source="model-family";}
#else
    platform="macos";model=sysctl_text("hw.model");
    const auto battery=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("AppleSmartBattery"));
    if(battery){IOObjectRelease(battery);form="laptop";source="internal-battery";}
    else if(model.starts_with("MacBook")){form="laptop";source="model-family";}
    else if(model.starts_with("Mac") || model.starts_with("iMac")){form="desktop";source="model-family";}
#endif
#elif defined(__ANDROID__)
    platform="android";model=property("ro.product.model");
    const auto characteristics=property("ro.build.characteristics");
    if(characteristics.find("tablet")!=std::string::npos){form="tablet";source="platform-property";}
    else if(characteristics.find("tv")!=std::string::npos){form="tv";source="platform-property";}
    else if(characteristics.find("watch")!=std::string::npos){form="watch";source="platform-property";}
    else if(characteristics.find("phone")!=std::string::npos){form="phone";source="platform-property";}
    else if(property("ro.product.manufacturer")=="Google" && model.starts_with("Pixel")){
      form=model=="Pixel Tablet"?"tablet":"phone";source="model-family";
    }
#elif defined(_WIN32)
    platform="windows";
    const DWORD provider=0x52534d42; // 'RSMB', as specified by GetSystemFirmwareTable.
    const auto size=GetSystemFirmwareTable(provider,0,nullptr,0);
    if(size>=8 && size<=1024*1024){
      std::vector<unsigned char> bytes(size);
      if(GetSystemFirmwareTable(provider,0,bytes.data(),size)==size){
        unsigned chassis=0;std::string maker;
        for(size_t pos=8;pos+4<=bytes.size();){
          const auto type=bytes[pos],length=bytes[pos+1];if(length<4 || pos+length>bytes.size())break;
          if(type==1 && length>5){maker=firmware_string(bytes,pos+length,bytes[pos+4]);model=firmware_string(bytes,pos+length,bytes[pos+5]);}
          if(type==3 && length>5)chassis=bytes[pos+5]&0x7f;
          size_t next=pos+length;while(next+1<bytes.size() && (bytes[next] || bytes[next+1]))++next;
          if(next+1>=bytes.size())break;pos=next+2;
        }
        form=libsys::device::Form(maker,model,chassis);if(form!="unknown")source="firmware";
      }
    }
#elif defined(__linux__)
    platform="linux";model=read("/sys/class/dmi/id/product_name");
    const auto maker=read("/sys/class/dmi/id/sys_vendor"),chassis=read("/sys/class/dmi/id/chassis_type");
    unsigned type=0;try{type=static_cast<unsigned>(std::stoul(chassis));}catch(...){}
    form=libsys::device::Form(maker,model,type);if(form!="unknown")source="firmware";
#endif
  }catch(...) {form="unknown";source="unknown";}
  copy(info.platform,platform);copy(info.form,form);copy(info.model,model);copy(info.source,source);
  *output=info;return 0;
}
