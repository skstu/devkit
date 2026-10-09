#pragma once
#include "libnet/net.h"
#include <cstring>
#include <utility>
#include <vector>
namespace libnet::detail {
// Internal value comparison used by the real watcher and deterministic tests.
// Never compare ABI padding bytes or retain OS-owned pointers.
struct InterfaceSnapshot {
  std::vector<dknet_interface> rows;
  uint64_t generation=0;
  static bool Equal(const dknet_interface& a,const dknet_interface& b) {
    return a.family==b.family && a.index==b.index && a.scope_id==b.scope_id && a.flags==b.flags &&
      !std::strcmp(a.name,b.name) && !std::strcmp(a.address,b.address) &&
      !std::strcmp(a.netmask,b.netmask) && !std::strcmp(a.broadcast,b.broadcast);
  }
  bool Update(std::vector<dknet_interface> next) {
    if(generation && rows.size()==next.size()) {
      bool same=true;
      for(size_t i=0;i<rows.size();++i) if(!Equal(rows[i],next[i])) { same=false; break; }
      if(same) return false;
    }
    rows=std::move(next); ++generation; return true;
  }
};
}
