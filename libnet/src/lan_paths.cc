#include "lan_paths.h"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <set>
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <SystemConfiguration/SystemConfiguration.h>
#endif
#elif defined(__ANDROID__)
#include <android/multinetwork.h>
#include <jni.h>
#endif

namespace libnet::detail {
uint32_t InterfaceFlags(const char *name) {
#ifdef _WIN32
  ULONG bytes=16384;std::vector<unsigned char> storage(bytes);
  auto *rows=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  auto code=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,rows,&bytes);
  if(code==ERROR_BUFFER_OVERFLOW && bytes<=1048576) {
    storage.resize(bytes);rows=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    code=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,rows,&bytes);
  }
  if(code!=NO_ERROR)return 0;
  wchar_t alias[256]{};
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,alias,256))return 0;
  for(auto *row=rows;row;row=row->Next)if(row->FriendlyName && !std::wcscmp(row->FriendlyName,alias))
    return (row->OperStatus==IfOperStatusUp?DKNET_INTERFACE_UP:0u) |
      ((row->IfType==IF_TYPE_TUNNEL || row->IfType==IF_TYPE_PPP)?DKNET_INTERFACE_TUNNEL:0u);
  return 0;
#else
  ifaddrs *rows=nullptr;if(getifaddrs(&rows))return 0;
  uint32_t flags=0;
  for(auto *row=rows;row;row=row->ifa_next)if(row->ifa_name && !std::strcmp(row->ifa_name,name)) {
    if(row->ifa_flags&IFF_UP)flags|=DKNET_INTERFACE_UP;
    if(row->ifa_flags&IFF_POINTOPOINT)flags|=DKNET_INTERFACE_TUNNEL;
  }
  freeifaddrs(rows);return flags;
#endif
}
bool SyntheticAddress(const char *value) {
  const auto p=Endpoint::Parse(value,0);if(!p)return true;
  if(p->family()==AddressFamily::ipv4) {
    const auto ip=ntohl(reinterpret_cast<const sockaddr_in*>(p->sockaddr_ptr())->sin_addr.s_addr);
    return (ip&0xfffe0000u)==0xc6120000u; // benchmark/fake-IP pool 198.18/15
  }
  const auto& ip=reinterpret_cast<const sockaddr_in6*>(p->sockaddr_ptr())->sin6_addr;
  return ip.s6_addr[0]==0x20 && ip.s6_addr[1]==0x01 && ip.s6_addr[2]==0 && ip.s6_addr[3]==2 &&
    ip.s6_addr[4]==0 && ip.s6_addr[5]==0; // benchmark/fake-IP prefix 2001:2::/48
}
#ifdef __ANDROID__
namespace {
std::mutex android_gate;JavaVM *android_vm=nullptr;jobject application=nullptr;
struct Env {
  JNIEnv *env=nullptr;bool attached=false, frame=false;
  Env() {
    if(!android_vm)return;
    if(android_vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_EDETACHED)
      attached=android_vm->AttachCurrentThread(&env,nullptr)==JNI_OK;
    if(env){frame=env->PushLocalFrame(128)==JNI_OK;if(!frame && env->ExceptionCheck())env->ExceptionClear();}
  }
  ~Env(){if(frame)env->PopLocalFrame(nullptr);if(attached)android_vm->DetachCurrentThread();}
  bool valid(){if(!env || !frame)return false;if(env->ExceptionCheck()){env->ExceptionClear();return false;}return true;}
  jmethodID method(jobject o,const char *name,const char *signature) {
    if(!o)return nullptr;auto type=env->GetObjectClass(o);if(!valid() || !type)return nullptr;auto m=env->GetMethodID(type,name,signature);
    env->DeleteLocalRef(type);return valid()?m:nullptr;
  }
};
bool AndroidNetworks(std::map<std::string,uint64_t>& output,std::string& preferred,std::set<std::string>* vpn_interfaces=nullptr) {
  std::lock_guard lock(android_gate);Env e;if(!e.valid() || !application)return false;
  auto service=e.method(application,"getSystemService","(Ljava/lang/String;)Ljava/lang/Object;");if(!service)return false;
  auto name=e.env->NewStringUTF("connectivity");auto manager=e.env->CallObjectMethod(application,service,name);
  if(!e.valid() || !manager)return false;
  auto all=e.method(manager,"getAllNetworks","()[Landroid/net/Network;");
  auto caps=e.method(manager,"getNetworkCapabilities","(Landroid/net/Network;)Landroid/net/NetworkCapabilities;");
  auto properties=e.method(manager,"getLinkProperties","(Landroid/net/Network;)Landroid/net/LinkProperties;");
  if(!all || !caps || !properties)return false;
  auto networks=static_cast<jobjectArray>(e.env->CallObjectMethod(manager,all));
  if(!e.valid() || !networks)return false;
  const int count=e.env->GetArrayLength(networks);if(count>32)return false;
  for(int i=0;i<count;++i) {
    auto network=e.env->GetObjectArrayElement(networks,i);
    auto capability=e.env->CallObjectMethod(manager,caps,network);
    if(!e.valid())return false;if(!capability)continue;
    auto has=e.method(capability,"hasCapability","(I)Z");
    if(!has)return false;
    const bool non_vpn=e.env->CallBooleanMethod(capability,has,jint(15)); // NET_CAPABILITY_NOT_VPN
    if(!e.valid())return false;
    auto links=e.env->CallObjectMethod(manager,properties,network);
    if(!e.valid())return false;if(!links)continue;
    auto interface=e.method(links,"getInterfaceName","()Ljava/lang/String;");
    auto handle=e.method(network,"getNetworkHandle","()J");
    if(!interface || !handle)return false;
    auto value=static_cast<jstring>(e.env->CallObjectMethod(links,interface));
    const auto number=e.env->CallLongMethod(network,handle);
    if(!e.valid() || !value || !number)return false;
    const char *bytes=e.env->GetStringUTFChars(value,nullptr);if(!bytes)return false;
    output.emplace(bytes,static_cast<uint64_t>(number));
    if(!non_vpn && vpn_interfaces)vpn_interfaces->emplace(bytes);
    auto transport=e.method(capability,"hasTransport","(I)Z");
    if(non_vpn && transport && e.env->CallBooleanMethod(capability,transport,jint(1)))preferred=bytes; // WIFI
    e.env->ReleaseStringUTFChars(value,bytes);if(!e.valid())return false;
  }
  return e.valid();
}
}
#endif
int AndroidInitialize(void *environment,void *context) {
#ifdef __ANDROID__
  auto *env=static_cast<JNIEnv*>(environment);if(!env)return DKNET_INVALID;
  std::lock_guard lock(android_gate);
  if(!context){if(application)env->DeleteGlobalRef(application);application=nullptr;android_vm=nullptr;return DKNET_OK;}
  JavaVM *vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK)return DKNET_STATE;
  auto type=env->GetObjectClass(static_cast<jobject>(context));
  auto method=env->GetMethodID(type,"getApplicationContext","()Landroid/content/Context;");
  auto app=method?env->CallObjectMethod(static_cast<jobject>(context),method):nullptr;
  if(env->ExceptionCheck()){env->ExceptionClear();env->DeleteLocalRef(type);return DKNET_INVALID;}
  if(!app){env->DeleteLocalRef(type);return DKNET_INVALID;}
  auto replacement=env->NewGlobalRef(app);env->DeleteLocalRef(app);env->DeleteLocalRef(type);
  if(env->ExceptionCheck()){env->ExceptionClear();return DKNET_INTERNAL;}
  if(!replacement)return DKNET_INTERNAL;
  if(application)env->DeleteGlobalRef(application);
  application=replacement;android_vm=vm;return DKNET_OK;
#else
  (void)environment;(void)context;return DKNET_STATE;
#endif
}
bool MatchesLanInterface(const dknet_interface& row,const char* value) {
  if(!value || !*value)return true;
  const auto selected=Endpoint::Parse(value,0), local=Endpoint::Parse(row.address,0);
  if(!selected || !local || static_cast<unsigned>(selected->family())!=row.family)return false;
  if(row.family==4)return selected->address()==local->address();
  const auto* requested=reinterpret_cast<const sockaddr_in6*>(selected->sockaddr_ptr());
  const auto* current=reinterpret_cast<const sockaddr_in6*>(local->sockaddr_ptr());
  if(requested->sin6_scope_id && requested->sin6_scope_id!=row.index)return false;
  // ::%index is the documented multicast interface selector. A concrete
  // scoped address must still match the address, not just the interface.
  return (requested->sin6_scope_id && IN6_IS_ADDR_UNSPECIFIED(&requested->sin6_addr)) ||
    !std::memcmp(&requested->sin6_addr,&current->sin6_addr,sizeof(in6_addr));
}
int ListLanPaths(const char *explicit_address,size_t maximum,std::vector<LanPath>& output) {
  output.clear();size_t count=0;
  int code=dknet_interface_list(nullptr,0,&count);if(code)return code;
  std::vector<dknet_interface> rows(count);
  code=dknet_interface_list(rows.data(),rows.size(),&count);if(code)return code;
  std::map<std::string,uint64_t> networks;std::string preferred;
#ifdef __ANDROID__
  std::set<std::string> vpn_interfaces;
  const bool android_ready=AndroidNetworks(networks,preferred,&vpn_interfaces);
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
  auto value=SCDynamicStoreCopyValue(nullptr,CFSTR("State:/Network/Global/IPv4"));
  if(value && CFGetTypeID(value)==CFDictionaryGetTypeID()) {
    auto name=CFDictionaryGetValue(static_cast<CFDictionaryRef>(value),CFSTR("PrimaryInterface"));char bytes[256]{};
    if(name && CFGetTypeID(name)==CFStringGetTypeID() && CFStringGetCString(static_cast<CFStringRef>(name),bytes,sizeof(bytes),kCFStringEncodingUTF8))preferred=bytes;
  }
  if(value)CFRelease(value);
#endif
  for(const auto& row:rows) {
    const bool explicit_match=explicit_address && *explicit_address && MatchesLanInterface(row,explicit_address);
    if(explicit_address && *explicit_address && !explicit_match)continue;
    if(!(row.flags&DKNET_INTERFACE_UP) || SyntheticAddress(row.address))continue;
    if(!explicit_match && (row.flags&DKNET_INTERFACE_INTERNAL))continue;
#ifdef __ANDROID__
    if(!(row.flags&DKNET_INTERFACE_INTERNAL) && (!android_ready || !networks.contains(row.name)))continue;
#endif
    if(output.size()>=maximum)return DKNET_BUSY;
    output.push_back({row,networks.contains(row.name)?networks[row.name]:0,preferred==row.name});
#ifdef __ANDROID__
    if(vpn_interfaces.contains(row.name))output.back().row.flags|=DKNET_INTERFACE_TUNNEL;
#endif
  }
  return output.empty()?DKNET_NOT_FOUND:DKNET_OK;
}
const LanPath *SelectLanPath(const std::vector<LanPath>& paths,const Endpoint& peer) {
  if(SyntheticAddress(peer.address().c_str()))return nullptr;
  const LanPath *best=nullptr;int best_score=-1;
  for(const auto& path:paths) {
    if(path.row.family!=static_cast<unsigned>(peer.family()))continue;
    const auto local=Endpoint::Parse(path.row.address,0), mask=Endpoint::Parse(path.row.netmask,0);
    if(!local || !mask)continue;
    const unsigned char *p,*l,*m;size_t size;
    if(peer.family()==AddressFamily::ipv4) {
      p=reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(peer.sockaddr_ptr())->sin_addr);
      l=reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(local->sockaddr_ptr())->sin_addr);
      m=reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(mask->sockaddr_ptr())->sin_addr);size=4;
    } else {
      const auto *remote=reinterpret_cast<const sockaddr_in6*>(peer.sockaddr_ptr());
      if(remote->sin6_scope_id && remote->sin6_scope_id!=path.row.index)continue;
      p=remote->sin6_addr.s6_addr;l=reinterpret_cast<const sockaddr_in6*>(local->sockaddr_ptr())->sin6_addr.s6_addr;
      m=reinterpret_cast<const sockaddr_in6*>(mask->sockaddr_ptr())->sin6_addr.s6_addr;size=16;
      const bool remote_link=IN6_IS_ADDR_LINKLOCAL(&remote->sin6_addr);
      if(remote_link!=bool(path.row.flags&DKNET_INTERFACE_LINK_LOCAL))continue;
    }
    bool on_link=true;int prefix=0;
    for(size_t i=0;i<size;++i){on_link&=(p[i]&m[i])==(l[i]&m[i]);for(unsigned bit=0;bit<8;++bit)prefix+=(m[i]>>bit)&1u;}
    // Tunnel subnets may be genuinely reachable, but a broad tunnel default
    // must not steal unrelated LAN destinations. Explicit selection is separate.
    const bool tunnel=path.row.flags&DKNET_INTERFACE_TUNNEL;
    const bool subnet=on_link && prefix && (!tunnel || prefix >= (size==4?8:16));
    int score=subnet?1000+prefix:tunnel?-1:path.preferred?20:10;
    if(score>best_score){best=&path;best_score=score;}
  }
  return best;
}
bool PrepareLanSocket(const LanPath& path,uintptr_t socket) {
  const auto flags=InterfaceFlags(path.row.name);
  if(!(flags&DKNET_INTERFACE_UP))return false;
#ifdef __ANDROID__
  if(path.row.flags&DKNET_INTERFACE_INTERNAL)return true;
  // A Wi-Fi Network handle can change while its interface/IP stays identical.
  // Resolve that same interface again on create/rebind; never substitute a VPN.
  std::map<std::string,uint64_t> current;std::string preferred;
  if(!AndroidNetworks(current,preferred) || !current.contains(path.row.name))return false;
  return android_setsocknetwork(current[path.row.name],static_cast<int>(socket))==0;
#elif defined(__APPLE__)
  const auto index=if_nametoindex(path.row.name);if(!index)return false;
  return setsockopt(static_cast<int>(socket),path.row.family==4?IPPROTO_IP:IPPROTO_IPV6,
    path.row.family==4?IP_BOUND_IF:IPV6_BOUND_IF,&index,sizeof(index))==0;
#elif defined(_WIN32)
  const DWORD index=path.row.family==4?htonl(path.row.index):path.row.index;if(!index)return false;
  return setsockopt(static_cast<SOCKET>(socket),path.row.family==4?IPPROTO_IP:IPPROTO_IPV6,
    path.row.family==4?IP_UNICAST_IF:IPV6_UNICAST_IF,reinterpret_cast<const char*>(&index),sizeof(index))==0;
#elif defined(__linux__)
  if(path.row.flags&DKNET_INTERFACE_INTERNAL)return true;
  return setsockopt(static_cast<int>(socket),SOL_SOCKET,SO_BINDTODEVICE,path.row.name,std::strlen(path.row.name)+1)==0;
#else
  return false;
#endif
}
}
