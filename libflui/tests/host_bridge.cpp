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
void flui_host_file_result(uint64_t,int32_t,const char*);
}
std::atomic<int> wake_count{0},dispatch_count{0};
void wake(){++wake_count;}
int32_t parser(const char*,uint64_t,flui_xml_callback,void*){return 0;}
void event(const flui_event*,void*){}
void invoke(void*){++dispatch_count;}
int files=0;
std::string selected;
void file_result(flui_status status,flui_string path,void*){assert(status==FLUI_OK);++files;selected.assign(path.data,path.size);}

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
 const flui_string popup_key{"popup",5},popup_value{"{}",2};
 assert(flui_window_property(h,popup_key,popup_value)==FLUI_UNSUPPORTED);
 while(flui_host_take()){}
 const flui_string drop_key{"dropfiles",9};
 assert(flui_window_property(h,drop_key,{"bad",3})==FLUI_INVALID_ARGUMENT);
 assert(flui_window_property(h,drop_key,{"true",4})==FLUI_OK);
 assert(std::string(flui_host_take()).find("\"enabled\":true")!=std::string::npos);
 const flui_string title{"Select",6};
 assert(flui_file_dialog_async(99,title,file_result,nullptr)==FLUI_INVALID_ARGUMENT);
 assert(flui_file_save_async(title,{"../bad",6},file_result,nullptr)==FLUI_INVALID_ARGUMENT);
 std::thread wrong([&]{assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_WRONG_THREAD);});wrong.join();
 assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_OK);
 assert(files==0);
 assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_BUSY);
 const std::string command=flui_host_take();
 const auto at=command.find("\"id\":");assert(at!=command.npos);
 const auto id=std::stoull(command.substr(at+5));
 flui_host_file_result(id+1,FLUI_OK,"wrong");assert(files==0);
 flui_host_file_result(id,FLUI_OK,"/tmp/chosen.txt");assert(files==1&&selected=="/tmp/chosen.txt");
 flui_host_file_result(id,FLUI_OK,"duplicate");assert(files==1);
 assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_OK);
 const std::string cancelled=flui_host_take();
 const auto cancel_id=std::stoull(cancelled.substr(cancelled.find("\"id\":")+5));
 flui_host_file_result(cancel_id,FLUI_OK,"");assert(files==2&&selected.empty());

 assert(flui_file_save_async(title,{"received.txt",12},file_result,nullptr)==FLUI_OK);
 assert(flui_file_dialog_async(FLUI_FILE_SAVE,title,file_result,nullptr)==FLUI_BUSY);
 const std::string saved=flui_host_take();
 assert(saved.find("\"kind\":2")!=saved.npos && saved.find("received.txt")!=saved.npos);
 const auto save_id=std::stoull(saved.substr(saved.find("\"id\":")+5));
 assert(flui_window_close(h)==FLUI_OK);
 const std::string closed=flui_host_take();
 assert(closed.find("\"op\":\"close\"")!=closed.npos && closed.find("\"id\":")==closed.npos);
 flui_host_file_result(save_id,FLUI_OK,"/tmp/export.txt");assert(files==3&&selected=="/tmp/export.txt");
 flui_host_file_result(save_id,FLUI_OK,"late");assert(files==3);
 assert(flui_file_dialog_async(FLUI_FILE_SAVE,title,file_result,nullptr)==FLUI_OK);
 const std::string save_cancel=flui_host_take();
 assert(save_cancel.find("\"op\":\"file\"")!=save_cancel.npos && save_cancel.find("\"id\":")!=save_cancel.npos);
 flui_host_file_result(std::stoull(save_cancel.substr(save_cancel.find("\"id\":")+5)),FLUI_OK,"");
 assert(files==4 && selected.empty());
 const flui_string source{"/tmp/source.txt",15},name{"source.txt",10};
 assert(flui_file_action_async(99,source,title,name,file_result,nullptr)==FLUI_INVALID_ARGUMENT);
 assert(flui_file_action_async(FLUI_FILE_EXPORT,source,title,name,file_result,nullptr)==FLUI_OK);
 assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_BUSY);
 const std::string action=flui_host_take();
 assert(action.find("\"op\":\"file_action\"")!=action.npos && action.find("/tmp/source.txt")!=action.npos);
 const auto action_id=std::stoull(action.substr(action.find("\"id\":")+5));
 flui_host_file_result(action_id,FLUI_OK,"content://export/result");assert(files==5&&selected=="content://export/result");
 flui_host_file_result(action_id,FLUI_OK,"late");assert(files==5);
 assert(flui_window_patch(h,input,18)==FLUI_INVALID_HANDLE);
 const flui_string badge_key{"badge",5};
 assert(flui_window_property(h,badge_key,{"+2",2})==FLUI_UNSUPPORTED);
 assert(flui_file_dialog_async(FLUI_FILE_MEDIA,title,file_result,nullptr)==FLUI_OK);
 const std::string media=flui_host_take();assert(media.find("\"kind\":6")!=media.npos);
 const auto media_id=std::stoull(media.substr(media.find("\"id\":")+5));
 assert(flui_file_dialog_async(FLUI_FILE_OPEN,title,file_result,nullptr)==FLUI_BUSY);
 flui_host_file_result(media_id,FLUI_OK,"/tmp/selected.heic");assert(files==6&&selected=="/tmp/selected.heic");
 flui_host_file_result(media_id,FLUI_OK,"duplicate");assert(files==6);
 assert(flui_file_dialog_async(FLUI_FILE_MEDIA_MULTIPLE,title,file_result,nullptr)==FLUI_OK);
 const std::string batch=flui_host_take();assert(batch.find("\"kind\":7")!=batch.npos);
 const auto batch_id=std::stoull(batch.substr(batch.find("\"id\":")+5));
 const std::string paths="[\"/tmp/one.png\",\"/tmp/two.jpg\"]";
 flui_host_file_result(batch_id,FLUI_OK,paths.c_str());assert(files==7&&selected==paths);
 flui_host_file_result(batch_id,FLUI_OK,"late");assert(files==7);
 assert(flui_file_dialog_async(FLUI_FILE_MEDIA_MULTIPLE,title,file_result,nullptr)==FLUI_OK);
 const std::string cancel=flui_host_take();const auto media_cancel_id=std::stoull(cancel.substr(cancel.find("\"id\":")+5));
 flui_host_file_result(media_cancel_id,FLUI_OK,"");assert(files==8&&selected.empty());
 assert(flui_window_destroy(h)==FLUI_OK);
 assert(flui_window_destroy(h)==FLUI_INVALID_HANDLE);
}
