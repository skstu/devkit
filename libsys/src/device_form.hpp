#pragma once
#include <string_view>
namespace libsys::device {
inline const char* Form(std::string_view maker,std::string_view model,unsigned chassis) {
  // Known Microsoft families supplement firmware that reports a detachable as
  // a notebook. Unknown models never acquire a form merely from their OS.
  if(maker.starts_with("Microsoft") && model.starts_with("Surface Pro"))return "tablet";
  if(maker.starts_with("Microsoft") && model.starts_with("Surface Laptop"))return "laptop";
  switch(chassis){
    case 8:case 9:case 10:case 14:return "laptop";
    case 30:return "tablet";
    case 31:case 32:return "convertible";
    case 3:case 4:case 5:case 6:case 7:case 13:case 15:case 16:return "desktop";
    case 17:case 23:return "server";
    default:return "unknown";
  }
}
}
