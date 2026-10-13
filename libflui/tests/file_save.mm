#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <libflui/desktop.h>
#include <cstdio>
#include <string>
#include <thread>
static flui_window window;
static flui_timer timer;
static bool failed=false;
static int phase=0,ticks=0,completions=0;
static NSString *root,*source;
static flui_string span(const std::string& s){return {s.data(),s.size()};}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);failed=true;}}while(0)
static void event(const flui_event*,void*){}
static void completed(flui_status status,flui_string path,void*) {
  CHECK(NSThread.isMainThread);CHECK(status==FLUI_OK);++completions;
  if(phase==1 || phase==3)CHECK(!path.size);
  if(phase==2) {
    NSString* target=[root stringByAppendingPathComponent:@"copy.bin"];
    CHECK(std::string(path.data,path.size)==target.UTF8String);
    CHECK([[NSData dataWithContentsOfFile:source] isEqual:[NSData dataWithContentsOfFile:target]]);
  }
  if(phase==3) {CHECK(completions==3);flui_app_quit();return;}
  ++phase;
  const std::string pathSource=source.UTF8String;
  CHECK(flui_file_action_async(FLUI_FILE_EXPORT,span(pathSource),span("flui-save-fixture"),span("copy.bin"),completed,nullptr)==FLUI_OK);
}
// The system dialog is hosted outside NSApp.windows on current macOS.
// Isolate panel decisions here; real OS presentation is a normal-App gate.
@interface FixtureSavePanel : NSObject
@property(nonatomic,copy) NSString* title;
@property(nonatomic,copy) NSString* nameFieldStringValue;
@property(nonatomic,strong) NSURL* URL;
- (void)beginWithCompletionHandler:(void(^)(NSModalResponse))completion;
@end
@implementation FixtureSavePanel
- (void)beginWithCompletionHandler:(void(^)(NSModalResponse))completion {
  if(phase==1)CHECK([self.nameFieldStringValue isEqualToString:@"received.bin"]);
  if(phase==2)self.URL=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:@"copy.bin"]];
  const bool accept=phase==2;
  dispatch_async(dispatch_get_main_queue(),^{completion(accept?NSModalResponseOK:NSModalResponseCancel);});
}
@end
static id fixturePanel(id,SEL) {return [FixtureSavePanel new];}
static void poll(void*) {
  if(++ticks>250){failed=true;std::fprintf(stderr,"fixture callback timeout phase=%d\n",phase);flui_app_quit();}
}

int main() {
  @autoreleasepool {
    root=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    CHECK([NSFileManager.defaultManager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil]);
    source=[root stringByAppendingPathComponent:@"received.bin"];
    CHECK([[@"synthetic file only" dataUsingEncoding:NSUTF8StringEncoding] writeToFile:source atomically:YES]);
    const std::string title="File action fixture";
    flui_window_options o{sizeof(o),FLUI_ABI_VERSION,400,300,span(title),event,nullptr};
    CHECK(flui_window_create(&o,&window)==FLUI_OK);
    CHECK(flui_file_save_async(span("fixture"),span("../bad"),completed,nullptr)==FLUI_INVALID_ARGUMENT);
    std::thread wrong([]{CHECK(flui_file_save_async(span("fixture"),span("name"),completed,nullptr)==FLUI_WRONG_THREAD);});wrong.join();
    Method factory=class_getClassMethod(NSSavePanel.class,@selector(savePanel));
    const IMP original=method_setImplementation(factory,(IMP)fixturePanel);
    phase=1;
    CHECK(flui_file_save_async(span("flui-save-fixture"),span("received.bin"),completed,nullptr)==FLUI_OK);
    CHECK(flui_file_dialog_async(FLUI_FILE_OPEN,span("fixture"),completed,nullptr)==FLUI_BUSY);
    CHECK(flui_timer_create(30,poll,nullptr,&timer)==FLUI_OK);
    CHECK(flui_app_run()==FLUI_OK);
    method_setImplementation(factory,original);
    flui_timer_destroy(timer);flui_window_close(window);flui_window_destroy(window);
    CHECK([[NSData dataWithContentsOfFile:source] isEqual:[@"synthetic file only" dataUsingEncoding:NSUTF8StringEncoding]]);
    [NSFileManager.defaultManager removeItemAtPath:root error:nil];
    if(!failed)std::puts("PASS native ABI with isolated panel provider: suggested name, cancel, busy, UI ownership, copy completion and source retained");
    return failed?1:0;
  }
}
