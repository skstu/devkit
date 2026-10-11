#import <Cocoa/Cocoa.h>
#include <libflui/desktop.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
static bool failed;
static int closed;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failed=true; } } while (0)
static flui_string span(const std::string& s) { return {s.data(),s.size()}; }
static void event(const flui_event* e, void*) { if(e->kind==FLUI_EVENT_CLOSED)++closed; }
static NSWindow* native(NSString* title) {
  for(NSWindow* w in NSApp.windows)if([w.title isEqualToString:title])return w;
  return nil;
}
static std::string options(flui_window parent, const char* anchor="20,40,40,40", int gap=8) {
  return "{\"parent\":\""+std::to_string(parent)+"\",\"anchor\":["+anchor+"],\"gap\":"+std::to_string(gap)+"}";
}
int main() {
  flui_window parent=0,popup=0;
  const std::string parentTitle="Anchor fixture",popupTitle="Popup fixture";
  flui_window_options o{sizeof(o),FLUI_ABI_VERSION,400,300,span(parentTitle),event,nullptr};
  CHECK(flui_window_create(&o,&parent)==FLUI_OK);
  o.width=280;o.height=180;o.title=span(popupTitle);
  CHECK(flui_window_create(&o,&popup)==FLUI_OK);
  if(!parent || !popup)return 1;
  const auto set=[&](const std::string& s){return flui_window_property(popup,span("popup"),span(s));};
  CHECK(set("{}") == FLUI_INVALID_ARGUMENT);
  CHECK(set("[]") == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(999999)) == FLUI_INVALID_HANDLE);
  CHECK(set(options(popup)) == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(parent,"-1,40,40,40")) == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(parent,"20,40,0,40")) == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(parent,"399,40,40,40")) == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(parent,"20,40,true,40")) == FLUI_INVALID_ARGUMENT);
  CHECK(set(options(parent,"20,40,40,40",65)) == FLUI_INVALID_ARGUMENT);
  std::thread other([&]{CHECK(set(options(parent))==FLUI_WRONG_THREAD);});other.join();
  NSWindow* p=native(@"Anchor fixture");NSWindow* w=native(@"Popup fixture");
  CHECK(p && w);
  const NSRect visible=p.screen.visibleFrame;
  [p setFrameOrigin:NSMakePoint(NSMinX(visible)+100,NSMaxY(visible)-400)];
  CHECK(set(options(parent))==FLUI_OK);
  CHECK(w.styleMask==NSWindowStyleMaskBorderless && !w.movable && w.canBecomeKeyWindow && !w.canBecomeMainWindow);
  CHECK(w.parentWindow==p && p.childWindows.count==1);
  CHECK(NSWidth(w.frame)==280 && NSHeight(w.frame)==180);
  for(NSWindowButton b : {NSWindowCloseButton,NSWindowMiniaturizeButton,NSWindowZoomButton})
    CHECK(![w standardWindowButton:b] || [w standardWindowButton:b].hidden);
  CHECK(std::abs(NSMinX(w.frame)-(NSMinX(p.frame)+68))<1);
  const NSRect a=NSMakeRect(20,NSHeight(p.contentView.bounds)-80,40,40);
  const NSRect screen=[p convertRectToScreen:[p.contentView convertRect:a toView:nil]];
  CHECK(std::abs(NSMaxY(w.frame)-NSMaxY(screen))<1);
  CHECK(set(options(parent))==FLUI_OK && p.childWindows.count==1);
  CHECK(flui_window_property(parent,span("popup"),span(options(popup)))==FLUI_INVALID_ARGUMENT);
  // A parent move carries its popup; resize keeps its top-left anchor.
  const CGFloat before=NSMinX(w.frame);
  [p setFrameOrigin:NSMakePoint(NSMinX(p.frame)+30,NSMinY(p.frame))];
  CHECK(std::abs(NSMinX(w.frame)-before-30)<1);
  [p setFrameOrigin:NSMakePoint(NSMaxX(visible)-400,NSMinY(visible)+100)];
  CHECK(set(options(parent,"350,40,40,40"))==FLUI_OK);
  CHECK(NSMinX(w.frame)<NSMinX(p.frame)+350 && NSMaxX(w.frame)<=NSMaxX(visible));
  [p setFrameOrigin:NSMakePoint(NSMinX(visible),NSMinY(visible))];
  CHECK(set(options(parent,"20,250,40,40"))==FLUI_OK);
  CHECK(NSMinY(w.frame)>=NSMinY(visible));
  CHECK(flui_window_close(parent)==FLUI_OK && closed==2);
  CHECK(flui_window_show(popup)==FLUI_INVALID_HANDLE);
  CHECK(flui_window_destroy(popup)==FLUI_OK);
  CHECK(flui_window_destroy(parent)==FLUI_OK);
  if(!failed)std::puts("PASS popup ABI: validation, borderless key window, anchor placement, parent movement, display edges and close lifecycle");
  return failed?1:0;
}
