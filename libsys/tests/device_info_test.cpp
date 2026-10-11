#include <libsys/device_info.h>
#include "../src/device_form.hpp"
#include "../src/device_text.hpp"
#include <cstdio>
#include <cstring>
int main(){
  dksys_device_info i{};
  if(dksys_device_detect(nullptr)!=-1 || dksys_device_detect(&i)!=-1)return 1;
  i.struct_size=sizeof(i);i.version=1;
  if(dksys_device_detect(&i)!=0 || !i.platform[0] || !i.form[0] ||
     !std::memchr(i.model,0,sizeof(i.model)))return 2;
  using libsys::device::Form;
  if(std::strcmp(Form("Microsoft Corporation","Surface Pro 6",9),"tablet") ||
     std::strcmp(Form("Microsoft Corporation","Surface Laptop 3",9),"laptop") ||
     std::strcmp(Form("Other","Unrecognized",2),"unknown") ||
     std::strcmp(Form("Other","Desktop",3),"desktop") ||
     std::strcmp(Form("Other","Convertible",31),"convertible"))return 3;
  using libsys::device::Text;
  if(Text("平板",4)!="平" || !Text("\xF0\x80\x80\x80",10).empty() ||
     !Text("\xED\xA0\x80",10).empty() || !Text("\xFF",10).empty() ||
     !Text("bad\nmodel",20).empty())return 4;
  std::printf("PASS platform=%s form=%s model=%s source=%s\n",i.platform,i.form,i.model,i.source);
}
