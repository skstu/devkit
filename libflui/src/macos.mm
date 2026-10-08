#import <Cocoa/Cocoa.h>
#import <FlutterMacOS/FlutterMacOS.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <dlfcn.h>
#include <libflui/desktop.h>
#include <libflui/flui.h>

@interface FLUIWindow : NSObject <NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) FlutterEngine *engine;
@property(nonatomic, strong) FlutterMethodChannel *channel;
@property(nonatomic) flui_window handle;
@property(nonatomic) flui_event_callback callback;
@property(nonatomic) void *user;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) BOOL ready;
@property(nonatomic) BOOL closed;
@property(nonatomic) BOOL managedClose;
@property(nonatomic) BOOL modal;
@property(nonatomic) NSUInteger pending;
@property(nonatomic) NSUInteger pendingBytes;
- (void)emit:(uint32_t)kind
     request:(uint64_t)request
      status:(flui_status)status
        name:(NSString *)name
       value:(NSString *)value;
@end
static NSMutableDictionary<NSNumber *, FLUIWindow *> *windows;
static uint64_t nextHandle = 1;
static unsigned callbackDepth;
static BOOL running;
@interface FLUICommands : NSObject
- (void)quit:(id)sender;
@end
@implementation FLUICommands
- (void)quit:(id)sender {
  (void)sender;
  for (FLUIWindow *window in windows.allValues)
    flui_window_request_close(window.handle);
}
@end
static FLUICommands *commands;
static void installStandaloneMenu() {
  if (commands)
    return;
  commands = [FLUICommands new];
  NSMenu *menu = NSApp.mainMenu;
  if (!menu)
    menu = [NSMenu new];
  if (!menu.numberOfItems)
    [menu addItem:[NSMenuItem new]];
  NSMenuItem *appItem = [menu itemAtIndex:0];
  if (!appItem.submenu.numberOfItems) {
    NSMenu *appMenu = [NSMenu new];
    NSMenuItem *quit = [appMenu addItemWithTitle:@"Quit"
                                          action:@selector(quit:)
                                   keyEquivalent:@"q"];
    quit.target = commands;
    appItem.submenu = appMenu;
  }
  if ([menu indexOfItemWithTitle:@"Edit"] == -1) {
    NSMenuItem *editItem = [NSMenuItem new];
    editItem.title = @"Edit";
    NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    NSMenuItem *redo = [edit addItemWithTitle:@"Redo"
                                       action:@selector(redo:)
                                keyEquivalent:@"z"];
    redo.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagShift;
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:@"Paste"
                    action:@selector(paste:)
             keyEquivalent:@"v"];
    [edit addItemWithTitle:@"Select All"
                    action:@selector(selectAll:)
             keyEquivalent:@"a"];
    editItem.submenu = edit;
    [menu addItem:editItem];
  }
  NSApp.mainMenu = menu;
}
static NSString *string(flui_string s) {
  if ((!s.data && s.size) || s.size > 1024 * 1024)
    return nil;
  return [[NSString alloc] initWithBytes:s.data ? s.data : ""
                                  length:(NSUInteger)s.size
                                encoding:NSUTF8StringEncoding];
}
static FLUIWindow *lookup(flui_window h) { return windows[@(h)]; }
@implementation FLUIWindow
- (void)emit:(uint32_t)kind
     request:(uint64_t)request
      status:(flui_status)status
        name:(NSString *)name
       value:(NSString *)value {
  if (!self.callback)
    return;
  NSData *n = [name dataUsingEncoding:NSUTF8StringEncoding];
  NSData *v = [value dataUsingEncoding:NSUTF8StringEncoding];
  flui_event event = {sizeof(flui_event),
                      kind,
                      self.handle,
                      request,
                      status,
                      0,
                      {(const char *)n.bytes, n.length},
                      {(const char *)v.bytes, v.length}};
  ++callbackDepth;
  try {
    self.callback(&event, self.user);
  } catch (...) { /* Never unwind through engine callbacks. */
  }
  --callbackDepth;
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  if (!self.managedClose)
    return YES;
  [self emit:FLUI_EVENT_CLOSE_REQUEST
      request:0
       status:FLUI_OK
         name:@""
        value:@""];
  return NO;
}
- (void)windowWillClose:(NSNotification *)note {
  (void)note;
  self.closed = YES;
  if (self.modal)
    [NSApp stopModal];
  [self.timer invalidate];
  [self emit:FLUI_EVENT_CLOSED request:0 status:FLUI_OK name:@"" value:@""];
}
@end

uint32_t flui_abi_version(void) { return FLUI_ABI_VERSION; }
flui_status flui_window_create(const flui_window_options *o, flui_window *out) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (!out)
    return FLUI_INVALID_ARGUMENT;
  *out = 0;
  if (!o || o->struct_size < sizeof(*o))
    return FLUI_INVALID_ARGUMENT;
  if (o->abi_version != FLUI_ABI_VERSION)
    return FLUI_ABI_MISMATCH;
  if (!o->on_event || o->width < 160 || o->height < 120 || o->width > 8192 ||
      o->height > 8192)
    return FLUI_INVALID_ARGUMENT;
  NSString *title = string(o->title);
  if (!title)
    return FLUI_INVALID_ARGUMENT;
  if (windows.count >= 8)
    return FLUI_LIMIT_EXCEEDED;
  @autoreleasepool {
    Dl_info image{};
    if (!dladdr((const void *)&flui_abi_version, &image))
      return FLUI_RUNTIME_ERROR;
    NSString *directory = [[NSString stringWithUTF8String:image.dli_fname]
        stringByDeletingLastPathComponent];
    NSString *path = [directory
        stringByAppendingPathComponent:
            @"libflui_runtime.bundle/Contents/Frameworks/App.framework"];
    NSBundle *bundle = [NSBundle bundleWithPath:path];
    if (!bundle || ![[NSFileManager defaultManager]
                       fileExistsAtPath:[bundle.resourcePath
                                            stringByAppendingPathComponent:
                                                @"flutter_assets"]])
      return FLUI_RUNTIME_ERROR;
    [NSApplication sharedApplication];
    if (!windows)
      windows = [NSMutableDictionary dictionary];
    FLUIWindow *w = [FLUIWindow new];
    w.handle = nextHandle++;
    w.callback = o->on_event;
    w.user = o->user;
    FlutterDartProject *project =
        [[FlutterDartProject alloc] initWithPrecompiledDartBundle:bundle];
    project.dartEntrypointArguments = @[];
    w.engine = [[FlutterEngine alloc] initWithName:@"devkit.libflui"
                                           project:project
                            allowHeadlessExecution:YES];
    w.channel =
        [FlutterMethodChannel methodChannelWithName:@"devkit.libflui/v1"
                                    binaryMessenger:w.engine.binaryMessenger];
    __weak FLUIWindow *weak = w;
    [w.channel
        setMethodCallHandler:^(FlutterMethodCall *call, FlutterResult reply) {
          FLUIWindow *owner = weak;
          if (!owner || owner.closed) {
            reply(nil);
            return;
          }
          if ([call.method isEqualToString:@"ready"]) {
            if (![call.arguments isEqual:@1]) {
              reply(nil);
              [owner emit:FLUI_EVENT_ERROR
                  request:0
                   status:FLUI_ABI_MISMATCH
                     name:@""
                    value:@"Renderer protocol mismatch"];
              return;
            }
            BOOL wasReady = owner.ready;
            owner.ready = YES;
            reply(nil);
            if (!wasReady)
              [owner emit:FLUI_EVENT_READY
                  request:0
                   status:FLUI_OK
                     name:@""
                    value:@""];
          } else if ([call.method isEqualToString:@"action"] &&
                     [call.arguments isKindOfClass:[NSDictionary class]]) {
            id name = call.arguments[@"name"], value = call.arguments[@"value"];
            reply(nil);
            if ([name isKindOfClass:[NSString class]] &&
                [value isKindOfClass:[NSString class]])
              [owner emit:FLUI_EVENT_ACTION
                  request:0
                   status:FLUI_OK
                     name:name
                    value:value];
          } else if ([call.method isEqualToString:@"error"]) {
            reply(nil);
            [owner emit:FLUI_EVENT_ERROR
                request:0
                 status:FLUI_RUNTIME_ERROR
                   name:@""
                  value:[call.arguments description]];
          } else
            reply(FlutterMethodNotImplemented);
        }];
    FlutterViewController *view =
        [[FlutterViewController alloc] initWithEngine:w.engine
                                              nibName:nil
                                               bundle:nil];
    w.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, o->width, o->height)
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    w.window.releasedWhenClosed = NO;
    w.window.title = title;
    w.window.contentViewController = view;
    // Attaching the initially zero-sized Flutter view may resize its NSWindow.
    [w.window setContentSize:NSMakeSize(o->width, o->height)];
    w.window.contentMinSize = NSMakeSize(160, 120);
    w.window.delegate = w;
    [w.window center];
    windows[@(w.handle)] = w;
    if (![w.engine runWithEntrypoint:nil]) {
      [w.channel setMethodCallHandler:nil];
      [w.engine shutDownEngine];
      w.window.delegate = nil;
      [w.window close];
      [windows removeObjectForKey:@(w.handle)];
      return FLUI_RUNTIME_ERROR;
    }
    *out = w.handle;
    return FLUI_OK;
  }
}
flui_status flui_window_show(flui_window handle) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(handle);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [w.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  return FLUI_OK;
}
static flui_status send(flui_window handle, flui_string input, uint64_t request,
                        NSString *method) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(handle);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  if (!w.ready)
    return FLUI_NOT_READY;
  if (input.size > 1024 * 1024)
    return FLUI_LIMIT_EXCEEDED;
  NSString *text = string(input);
  if (!text)
    return FLUI_INVALID_ARGUMENT;
  if (w.pending >= 16 || w.pendingBytes + input.size > 2 * 1024 * 1024)
    return FLUI_BUSY;
  ++w.pending;
  w.pendingBytes += input.size;
  __weak FLUIWindow *weak = w;
  [w.channel
      invokeMethod:method
         arguments:text
            result:^(id result) {
              FLUIWindow *owner = weak;
              if (!owner || !lookup(handle))
                return;
              --owner.pending;
              owner.pendingBytes -= input.size;
              if (owner.closed)
                return;
              BOOL failed = [result isKindOfClass:[FlutterError class]] ||
                            result == FlutterMethodNotImplemented;
              NSString *reason = [result isKindOfClass:[FlutterError class]]
                                     ? [(FlutterError *)result message]
                                     : @"Renderer method unavailable";
              [owner emit:FLUI_EVENT_COMPLETE
                  request:request
                   status:failed ? FLUI_DOCUMENT_ERROR : FLUI_OK
                     name:@""
                    value:failed ? reason : @""];
            }];
  return FLUI_OK;
}
flui_status flui_window_load_xml(flui_window w, flui_string s, uint64_t r) {
  return send(w, s, r, @"load");
}
flui_status flui_window_set_state(flui_window w, flui_string s, uint64_t r) {
  return send(w, s, r, @"state");
}
flui_status flui_window_set_tick(flui_window handle, uint32_t milliseconds) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(handle);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  if (milliseconds && (milliseconds < 100 || milliseconds > 60000))
    return FLUI_INVALID_ARGUMENT;
  [w.timer invalidate];
  w.timer = nil;
  if (milliseconds) {
    __weak FLUIWindow *weak = w;
    w.timer = [NSTimer timerWithTimeInterval:milliseconds / 1000.0
                                     repeats:YES
                                       block:^(NSTimer *timer) {
                                         (void)timer;
                                         FLUIWindow *owner = weak;
                                         if (owner && !owner.closed)
                                           [owner emit:FLUI_EVENT_TICK
                                               request:0
                                                status:FLUI_OK
                                                  name:@""
                                                 value:@""];
                                       }];
    [[NSRunLoop mainRunLoop] addTimer:w.timer forMode:NSRunLoopCommonModes];
  }
  return FLUI_OK;
}
flui_status flui_window_destroy(flui_window handle) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (callbackDepth)
    return FLUI_BUSY;
  FLUIWindow *w = lookup(handle);
  if (!w)
    return FLUI_INVALID_HANDLE;
  w.callback = nullptr;
  if (w.modal)
    [NSApp stopModal];
  [w.timer invalidate];
  w.closed = YES;
  [w.channel setMethodCallHandler:nil];
  w.window.delegate = nil;
  [w.window close];
  w.window.contentViewController = nil;
  w.engine.viewController = nil;
  w.channel = nil;
  [w.engine shutDownEngine];
  [windows removeObjectForKey:@(handle)];
  return FLUI_OK;
}
flui_status flui_app_run(void) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (running || callbackDepth)
    return FLUI_BUSY;
  if (!windows.count)
    return FLUI_NOT_READY;
  installStandaloneMenu();
  running = YES;
  @autoreleasepool {
    [NSApp run];
  }
  running = NO;
  return FLUI_OK;
}
static BOOL quitRequested = NO;
flui_status flui_app_quit(void) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (!running)
    return FLUI_NOT_READY;
  for (FLUIWindow *w in windows.allValues)
    if (w.modal) {
      quitRequested = YES;
      [NSApp stopModal];
      return FLUI_OK;
    }
  quitRequested = NO;
  [NSApp stop:nil];
  // Wake nextEventMatchingMask without a polling timer.
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:nil
                                       subtype:0
                                         data1:0
                                         data2:0]
           atStart:YES];
  return FLUI_OK;
}

flui_status flui_window_set_tree(flui_window w, flui_string s, uint64_t r) {
  return send(w, s, r, @"tree");
}
flui_status flui_window_patch(flui_window w, flui_string s, uint64_t r) {
  return send(w, s, r, @"patch");
}
flui_status flui_window_property(flui_window h, flui_string key,
                                 flui_string value) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(h);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  NSString *k = string(key), *v = string(value);
  if (!k || !v)
    return FLUI_INVALID_ARGUMENT;
  if ([k isEqualToString:@"title"])
    w.window.title = v;
  else if ([k isEqualToString:@"managed_close"])
    w.managedClose = [v isEqualToString:@"true"];
  else if ([k isEqualToString:@"size"] || [k isEqualToString:@"minimum"]) {
    NSArray<NSString *> *parts = [v componentsSeparatedByString:@","];
    if (parts.count != 2 || parts[0].intValue < 160 ||
        parts[1].intValue < 120 || parts[0].intValue > 8192 ||
        parts[1].intValue > 8192)
      return FLUI_INVALID_ARGUMENT;
    NSSize size = NSMakeSize(parts[0].intValue, parts[1].intValue);
    if ([k isEqualToString:@"size"])
      [w.window setContentSize:size];
    else
      w.window.contentMinSize = size;
  } else
    return FLUI_INVALID_ARGUMENT;
  return FLUI_OK;
}
flui_status flui_window_request_close(flui_window h) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(h);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  if (w.managedClose)
    [w emit:FLUI_EVENT_CLOSE_REQUEST
        request:0
         status:FLUI_OK
           name:@""
          value:@""];
  else
    [w.window close];
  return FLUI_OK;
}
flui_status flui_window_close(flui_window h) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  FLUIWindow *w = lookup(h);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  [w.window close];
  return FLUI_OK;
}
flui_status flui_window_run_modal(flui_window h) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (callbackDepth)
    return FLUI_BUSY;
  FLUIWindow *w = lookup(h);
  if (!w || w.closed)
    return FLUI_INVALID_HANDLE;
  if (w.modal)
    return FLUI_BUSY;
  w.modal = YES;
  [w.window makeKeyAndOrderFront:nil];
  [NSApp runModalForWindow:w.window];
  w.modal = NO;
  if (quitRequested)
    flui_app_quit();
  return FLUI_OK;
}
flui_status flui_dispatch(flui_callback callback, void *user) {
  if (!callback)
    return FLUI_INVALID_ARGUMENT;
  // Common modes also run while a host uses a modal event loop.
  CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, ^{
    callback(user);
  });
  CFRunLoopWakeUp(CFRunLoopGetMain());
  return FLUI_OK;
}
static NSMutableDictionary<NSNumber *, NSTimer *> *desktopTimers;
static uint64_t nextTimer = 1;
flui_status flui_timer_create(uint32_t interval, flui_callback callback,
                              void *user, flui_timer *out) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (!out || !callback || interval < 1 || interval > 3600000)
    return FLUI_INVALID_ARGUMENT;
  if (!desktopTimers)
    desktopTimers = [NSMutableDictionary dictionary];
  if (desktopTimers.count >= 128)
    return FLUI_LIMIT_EXCEEDED;
  const auto id = nextTimer++;
  NSTimer *timer = [NSTimer timerWithTimeInterval:interval / 1000.0
                                          repeats:YES
                                            block:^(NSTimer *t) {
                                              (void)t;
                                              callback(user);
                                            }];
  desktopTimers[@(id)] = timer;
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
  *out = id;
  return FLUI_OK;
}
flui_status flui_timer_destroy(flui_timer id) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  NSTimer *timer = desktopTimers[@(id)];
  if (!timer)
    return FLUI_INVALID_HANDLE;
  [timer invalidate];
  [desktopTimers removeObjectForKey:@(id)];
  return FLUI_OK;
}
static void textResult(NSString *text, flui_text_callback callback,
                       void *user) {
  NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
  callback({(const char *)data.bytes, data.length}, user);
}
flui_status flui_file_dialog(uint32_t kind, flui_string title,
                             flui_string initial, flui_string extensions,
                             flui_text_callback callback, void *user) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  if (callbackDepth)
    return FLUI_BUSY;
  NSString *t = string(title), *n = string(initial), *e = string(extensions);
  if (!callback || !t || !n || !e)
    return FLUI_INVALID_ARGUMENT;
  NSMutableArray<UTType *> *types = [NSMutableArray array];
  for (NSString *ext in [e componentsSeparatedByString:@","]) {
    if (ext.length) {
      UTType *type = [UTType typeWithFilenameExtension:ext];
      if (!type)
        return FLUI_INVALID_ARGUMENT;
      [types addObject:type];
    }
  }
  if (kind == FLUI_FILE_SAVE) {
    NSSavePanel *p = [NSSavePanel savePanel];
    p.title = t;
    p.nameFieldStringValue = n;
    if (types.count)
      p.allowedContentTypes = types;
    if ([p runModal] == NSModalResponseOK)
      textResult(p.URL.path, callback, user);
  } else if (kind == FLUI_FILE_OPEN || kind == FLUI_FILE_DIRECTORY) {
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.title = t;
    p.allowsMultipleSelection = NO;
    if (kind == FLUI_FILE_OPEN && types.count)
      p.allowedContentTypes = types;
    p.canChooseFiles = kind == FLUI_FILE_OPEN;
    p.canChooseDirectories = kind == FLUI_FILE_DIRECTORY;
    if ([p runModal] == NSModalResponseOK)
      textResult(p.URL.path, callback, user);
  } else
    return FLUI_INVALID_ARGUMENT;
  return FLUI_OK;
}
flui_status flui_executable_path(flui_text_callback callback, void *user) {
  if (!callback)
    return FLUI_INVALID_ARGUMENT;
  textResult(NSBundle.mainBundle.executablePath, callback, user);
  return FLUI_OK;
}
flui_status flui_write_file_atomic(flui_string path, flui_string content) {
  NSString *p = string(path);
  if (!p || !p.length || (content.size && !content.data))
    return FLUI_INVALID_ARGUMENT;
  if (content.size > 64u * 1024u * 1024u)
    return FLUI_LIMIT_EXCEEDED;
  NSData *data = [NSData dataWithBytes:content.data
                                length:(NSUInteger)content.size];
  return [data writeToFile:p options:NSDataWritingAtomic error:nil]
             ? FLUI_OK
             : FLUI_RUNTIME_ERROR;
}
flui_status flui_bell(void) {
  if (![NSThread isMainThread])
    return FLUI_WRONG_THREAD;
  NSBeep();
  return FLUI_OK;
}
@interface FLUIXMLVisitor : NSObject <NSXMLParserDelegate>
@property(nonatomic) flui_xml_callback visitor;
@property(nonatomic) void *user;
@property(nonatomic) NSUInteger depth;
@property(nonatomic) NSUInteger count;
@property(nonatomic) BOOL failed;
@end
@implementation FLUIXMLVisitor
- (BOOL)emit:(uint32_t)kind name:(NSString *)name value:(NSString *)value {
  NSData *n = [name dataUsingEncoding:NSUTF8StringEncoding],
         *v = [value dataUsingEncoding:NSUTF8StringEncoding];
  return self.visitor(kind, {(const char *)n.bytes, n.length},
                      {(const char *)v.bytes, v.length}, self.user) == 0;
}
- (void)parser:(NSXMLParser *)p
    didStartElement:(NSString *)name
       namespaceURI:(NSString *)uri
      qualifiedName:(NSString *)qualified
         attributes:(NSDictionary<NSString *, NSString *> *)attrs {
  (void)uri;
  (void)qualified;
  if (++self.depth > 64 || ++self.count > 16384 ||
      ![self emit:1 name:name value:@""]) {
    self.failed = YES;
    [p abortParsing];
    return;
  }
  for (NSString *key in attrs)
    if (![self emit:2 name:key value:attrs[key]]) {
      self.failed = YES;
      [p abortParsing];
      return;
    }
}
- (void)parser:(NSXMLParser *)p
    didEndElement:(NSString *)name
     namespaceURI:(NSString *)uri
    qualifiedName:(NSString *)qualified {
  (void)uri;
  (void)qualified;
  --self.depth;
  if (![self emit:3 name:name value:@""]) {
    self.failed = YES;
    [p abortParsing];
  }
}
- (void)parser:(NSXMLParser *)p foundCharacters:(NSString *)text {
  if ([text
          stringByTrimmingCharactersInSet:NSCharacterSet
                                              .whitespaceAndNewlineCharacterSet]
          .length) {
    self.failed = YES;
    [p abortParsing];
  }
}
@end
flui_status flui_xml_visit(flui_string xml, flui_xml_callback visitor,
                           void *user) {
  NSString *text = string(xml);
  if (!text || !visitor)
    return FLUI_INVALID_ARGUMENT;
  NSRegularExpression *reject = [NSRegularExpression
      regularExpressionWithPattern:@"<!\\s*(DOCTYPE|ENTITY)"
                           options:NSRegularExpressionCaseInsensitive
                             error:nil];
  if ([reject numberOfMatchesInString:text
                              options:0
                                range:NSMakeRange(0, text.length)])
    return FLUI_DOCUMENT_ERROR;
  NSXMLParser *p = [[NSXMLParser alloc]
      initWithData:[text dataUsingEncoding:NSUTF8StringEncoding]];
  p.shouldResolveExternalEntities = NO;
  FLUIXMLVisitor *v = [FLUIXMLVisitor new];
  v.visitor = visitor;
  v.user = user;
  p.delegate = v;
  return [p parse] && !v.failed ? FLUI_OK : FLUI_DOCUMENT_ERROR;
}
