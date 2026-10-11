#pragma once
#include <string_view>
namespace libsys::device {
// Return a complete UTF-8 prefix, rejecting malformed or control-bearing text.
inline std::string_view Text(std::string_view value, size_t capacity) {
  size_t prefix=0;
  for(size_t i=0;i<value.size();) {
    const auto c=static_cast<unsigned char>(value[i]);size_t n=1;unsigned code=c;
    if(c<32 || c==127)return {};
    if(c>=0xC2 && c<=0xDF){n=2;code=c&31;}
    else if(c>=0xE0 && c<=0xEF){n=3;code=c&15;}
    else if(c>=0xF0 && c<=0xF4){n=4;code=c&7;}
    else if(c>=128)return {};
    if(i+n>value.size())return {};
    for(size_t j=1;j<n;++j){const auto b=static_cast<unsigned char>(value[i+j]);if((b&0xC0)!=0x80)return {};code=(code<<6)|(b&63);}
    if((n==2 && code<0x80)||(n==3 && code<0x800)||(n==4 && code<0x10000)||code>0x10FFFF||(code>=0xD800 && code<=0xDFFF))return {};
    i+=n;if(i<=capacity)prefix=i;
  }
  return value.substr(0,prefix);
}
}
