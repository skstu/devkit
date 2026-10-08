// SDK-owned event-driven host bridge for Android, iOS, Windows and Linux.
// Business controllers use the public C ABI; only the private renderer uses flui_host_*.
#include <libflui/desktop.h>
#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <functional>
#include <cstdio>
#include <algorithm>
namespace {
std::thread::id owner;
std::mutex mutex;
std::deque<std::string> commands;
std::string delivered;
void (*wake)() = nullptr;
using XmlHost = int32_t (*)(const char*, uint64_t, flui_xml_callback, void*);
XmlHost xmlHost = nullptr;
struct Window { flui_window_options options{}; bool ready=false, closed=false, managed=false; unsigned pending=0; size_t bytes=0; };
std::map<uint64_t,Window> windows;
std::map<uint64_t,std::pair<flui_callback,void*>> callbacks;
uint64_t nextWindow=1,nextCallback=1;
unsigned callbackDepth=0;
bool ui() { return owner==std::this_thread::get_id(); }
std::string str(flui_string s) { return s.data ? std::string(s.data,size_t(s.size)) : ""; }
bool valid(flui_string s) { return (s.data || !s.size) && s.size<=1024*1024; }
std::string json(const std::string& s) {
 std::string out="\"";
 for(unsigned char c:s) {
  if(c=='"' || c=='\\') {out+='\\';out+=char(c);}
  else if(c<32) {char b[7]; std::snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}
  else out+=char(c);
 }
 return out+'"';
}
void queue(std::string s) {
 void(*notify)();
 {std::lock_guard<std::mutex> lock(mutex); commands.push_back(std::move(s)); notify=wake;}
 if(notify) notify();
}
void emit(uint64_t h,uint32_t kind,uint64_t request,int status,const std::string& name={},const std::string& value={}) {
 auto i=windows.find(h);if(i==windows.end()||!i->second.options.on_event)return;
 auto opt=i->second.options;
 flui_event e{sizeof(e),kind,h,request,status,0,{name.data(),name.size()},{value.data(),value.size()}};
 ++callbackDepth;
 try {opt.on_event(&e,opt.user);} catch(...) {}
 --callbackDepth;
}
flui_status send(flui_window h,flui_string s,uint64_t request,const char* op) {
 if(!ui())return FLUI_WRONG_THREAD;
 auto w=windows.find(h);if(w==windows.end()||w->second.closed)return FLUI_INVALID_HANDLE;
 if(!w->second.ready)return FLUI_NOT_READY;
 if(!valid(s))return s.size>1024*1024?FLUI_LIMIT_EXCEEDED:FLUI_INVALID_ARGUMENT;
 if(w->second.pending>=16||w->second.bytes+s.size>2*1024*1024)return FLUI_BUSY;
 ++w->second.pending;w->second.bytes+=s.size;
 // Request ids are caller-owned; accounting is carried in the private envelope.
 queue("{\"op\":"+json(op)+",\"window\":"+std::to_string(h)+",\"request\":"+std::to_string(request)+",\"bytes\":"+std::to_string(s.size)+",\"data\":"+json(str(s))+"}");
 return FLUI_OK;
}
}
extern "C" {
FLUI_API int32_t FLUI_CALL flui_host_attach(void(*notify)(), XmlHost xml) {
 if(!notify||!xml)return FLUI_INVALID_ARGUMENT;
 if(wake)return FLUI_BUSY;
 owner=std::this_thread::get_id();wake=notify;xmlHost=xml;return FLUI_OK;
}
FLUI_API const char* FLUI_CALL flui_host_take() {
 if(!ui())return nullptr;
 std::lock_guard<std::mutex> lock(mutex);
 if(commands.empty())return nullptr;
 delivered=std::move(commands.front());commands.pop_front();return delivered.c_str();
}
FLUI_API void FLUI_CALL flui_host_event(uint64_t h,uint32_t kind,uint64_t request,int32_t status,uint64_t bytes,const char* name,const char* value) {
 if(!ui())return;
 auto w=windows.find(h);if(w==windows.end())return;
 if(kind==FLUI_EVENT_READY)w->second.ready=true;
 if(kind==FLUI_EVENT_COMPLETE) {
  if(w->second.pending)--w->second.pending;
  w->second.bytes-=std::min(size_t(bytes),w->second.bytes);
 }
 if(kind==FLUI_EVENT_CLOSED)w->second.closed=true;
 emit(h,kind,request,status,name?name:"",value?value:"");
}
FLUI_API void FLUI_CALL flui_host_invoke(uint64_t id,uint32_t once) {
 if(!ui())return;
 std::pair<flui_callback,void*> fn;
 {std::lock_guard<std::mutex> lock(mutex);
  auto i=callbacks.find(id);if(i==callbacks.end())return;
  fn=i->second;if(once)callbacks.erase(i);
 }
 try {fn.first(fn.second);} catch(...) {}
}
uint32_t flui_abi_version(){return FLUI_ABI_VERSION;}
flui_status flui_window_create(const flui_window_options* o,flui_window* out) {
 if(!ui())return FLUI_WRONG_THREAD;if(!out)return FLUI_INVALID_ARGUMENT;*out=0;
 if(!o||o->struct_size<sizeof(*o)||!o->on_event||!valid(o->title))return FLUI_INVALID_ARGUMENT;
 if(o->abi_version!=FLUI_ABI_VERSION)return FLUI_ABI_MISMATCH;
 if(!windows.empty())return FLUI_LIMIT_EXCEEDED;
 auto h=nextWindow++;Window w;w.options=*o;w.options.title={nullptr,0};windows.emplace(h,w);*out=h;
 queue("{\"op\":\"create\",\"window\":"+std::to_string(h)+",\"title\":"+json(str(o->title))+"}");return FLUI_OK;
}
flui_status flui_window_show(flui_window h) {
 if(!ui())return FLUI_WRONG_THREAD;return windows.count(h)?FLUI_OK:FLUI_INVALID_HANDLE;
}
flui_status flui_window_load_xml(flui_window,flui_string,uint64_t){return FLUI_UNSUPPORTED;}
flui_status flui_window_set_state(flui_window,flui_string,uint64_t){return FLUI_UNSUPPORTED;}
flui_status flui_window_set_tree(flui_window h,flui_string s,uint64_t r){return send(h,s,r,"tree");}
flui_status flui_window_patch(flui_window h,flui_string s,uint64_t r){return send(h,s,r,"patch");}
flui_status flui_window_property(flui_window h,flui_string k,flui_string v) {
 if(!ui())return FLUI_WRONG_THREAD;if(!windows.count(h))return FLUI_INVALID_HANDLE;
 if(!valid(k)||!valid(v))return FLUI_INVALID_ARGUMENT;
 const auto key=str(k);
 if(key=="managed_close")windows[h].managed=str(v)=="true";
 else if(key=="title" || key=="size" || key=="minimum")return FLUI_UNSUPPORTED;
 else return FLUI_INVALID_ARGUMENT;
 return FLUI_OK;
}
flui_status flui_window_close(flui_window h) {
 if(!ui())return FLUI_WRONG_THREAD;if(!windows.count(h))return FLUI_INVALID_HANDLE;
 windows[h].closed=true;queue("{\"op\":\"close\",\"window\":"+std::to_string(h)+"}");return FLUI_OK;
}
flui_status flui_window_request_close(flui_window h) {
 if(!ui())return FLUI_WRONG_THREAD;if(!windows.count(h))return FLUI_INVALID_HANDLE;
 if(windows[h].managed)emit(h,FLUI_EVENT_CLOSE_REQUEST,0,FLUI_OK);else return flui_window_close(h);return FLUI_OK;
}
flui_status flui_window_destroy(flui_window h) {
 if(!ui())return FLUI_WRONG_THREAD;if(callbackDepth)return FLUI_BUSY;
 if(!windows.erase(h))return FLUI_INVALID_HANDLE;return FLUI_OK;
}
flui_status flui_app_run(){return FLUI_UNSUPPORTED;}
flui_status flui_app_quit(){if(!ui())return FLUI_WRONG_THREAD;return FLUI_OK;}
flui_status flui_window_run_modal(flui_window){return FLUI_UNSUPPORTED;}
flui_status flui_dispatch(flui_callback cb,void* user) {
 if(!cb)return FLUI_INVALID_ARGUMENT;
 uint64_t id;
 {std::lock_guard<std::mutex> lock(mutex); if(!wake)return FLUI_NOT_READY;id=nextCallback++;callbacks[id]={cb,user};}
 queue("{\"op\":\"dispatch\",\"id\":"+std::to_string(id)+"}");return FLUI_OK;
}
flui_status flui_timer_create(uint32_t ms,flui_callback cb,void* user,flui_timer* out) {
 if(!ui())return FLUI_WRONG_THREAD;if(!cb||!out||!ms||ms>60000)return FLUI_INVALID_ARGUMENT;
 uint64_t id;
 {std::lock_guard<std::mutex> lock(mutex);id=nextCallback++;callbacks[id]={cb,user};}
 *out=id;
 queue("{\"op\":\"timer\",\"id\":"+std::to_string(id)+",\"ms\":"+std::to_string(ms)+"}");return FLUI_OK;
}
flui_status flui_timer_destroy(flui_timer id) {
 if(!ui())return FLUI_WRONG_THREAD;
 {std::lock_guard<std::mutex> lock(mutex);if(!callbacks.erase(id))return FLUI_INVALID_HANDLE;}
 queue("{\"op\":\"timer\",\"id\":"+std::to_string(id)+",\"ms\":0}");return FLUI_OK;
}
flui_status flui_window_set_tick(flui_window,uint32_t){return FLUI_UNSUPPORTED;}
flui_status flui_file_dialog(uint32_t,flui_string,flui_string,flui_string,flui_text_callback,void*) {return FLUI_UNSUPPORTED;}
flui_status flui_executable_path(flui_text_callback,void*) {return FLUI_UNSUPPORTED;}
flui_status flui_write_file_atomic(flui_string,flui_string) {return FLUI_UNSUPPORTED;}
flui_status flui_bell(){return FLUI_UNSUPPORTED;}
flui_status flui_xml_visit(flui_string text,flui_xml_callback visitor,void* user) {
 if(!ui())return FLUI_WRONG_THREAD;
 if(!valid(text)||!visitor)return FLUI_INVALID_ARGUMENT;
 return xmlHost?xmlHost(text.data,text.size,visitor,user):FLUI_NOT_READY;
}
}
