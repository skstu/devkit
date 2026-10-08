#include <libflui/desktop.h>
#include <atomic>
#include <cassert>
#include <string>
#include <thread>
extern "C" {
int32_t flui_host_attach(void(*)(), int32_t(*)(const char*,uint64_t,flui_xml_callback,void*));
const char* flui_host_take();
void flui_host_event(uint64_t,uint32_t,uint64_t,int32_t,uint64_t,const char*,const char*);
void flui_host_invoke(uint64_t,uint32_t);
}
std::atomic<int> wake_count{0},dispatch_count{0};
void wake(){++wake_count;}
int32_t parser(const char*,uint64_t,flui_xml_callback,void*){return 0;}
void event(const flui_event*,void*){}
void invoke(void*){++dispatch_count;}
int main(){
 assert(flui_host_attach(wake,parser)==FLUI_OK);
 flui_window h=0;
 flui_window_options o{sizeof(o),FLUI_ABI_VERSION,320,548,{nullptr,0},event,nullptr};
 assert(flui_window_create(&o,&h)==FLUI_OK && h);
 std::string small="{}"; flui_string input{small.data(),small.size()};
 assert(flui_window_set_tree(h,input,1)==FLUI_NOT_READY);
 flui_host_take();
 flui_host_event(h,FLUI_EVENT_READY,0,FLUI_OK,0,"","");
 std::thread worker([&]{
   assert(flui_window_set_tree(h,input,1)==FLUI_WRONG_THREAD);
   for(int i=0;i<1000;++i) assert(flui_dispatch(invoke,nullptr)==FLUI_OK);
 });
 // Exercise concurrent insertion and UI removal of callback entries.
 while(dispatch_count<1000){
   if(const char* p=flui_host_take()){
     std::string cmd=p;auto offset=cmd.find("\"id\":");
     if(offset!=cmd.npos)flui_host_invoke(std::stoull(cmd.substr(offset+5)),1);
   } else std::this_thread::yield();
 }
 worker.join();
 assert(dispatch_count==1000);
 for(unsigned i=0;i<16;++i) assert(flui_window_patch(h,input,i)==FLUI_OK);
 assert(flui_window_patch(h,input,16)==FLUI_BUSY);
 flui_host_event(h,FLUI_EVENT_COMPLETE,0,FLUI_OK,small.size(),"","");
 assert(flui_window_patch(h,input,17)==FLUI_OK);
 flui_timer timer=0;
 assert(flui_timer_create(10,invoke,nullptr,&timer)==FLUI_OK);
 assert(flui_timer_destroy(timer)==FLUI_OK);
 flui_host_invoke(timer,0);
 assert(dispatch_count==1000);
 assert(flui_window_run_modal(h)==FLUI_UNSUPPORTED);
 assert(flui_window_close(h)==FLUI_OK);
 assert(flui_window_patch(h,input,18)==FLUI_INVALID_HANDLE);
 assert(flui_window_destroy(h)==FLUI_OK);
 assert(flui_window_destroy(h)==FLUI_INVALID_HANDLE);
}
