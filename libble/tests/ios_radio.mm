#include "radio_fixture.hpp"
#import <UIKit/UIKit.h>
#include <chrono>
#include <memory>
@interface BleLabDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UITextView *status;
@property(nonatomic, strong) NSTimer *timer;
@end
static std::unique_ptr<blelab::Fixture> fixture;
static std::chrono::steady_clock::time_point deadline;
static bool advertising = false, started = false;
static int duration = 0;
@implementation BleLabDelegate
- (BOOL)application:(UIApplication *)app
    didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)options;
  app.idleTimerDisabled = YES;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController *controller = [UIViewController new];
  self.window.rootViewController = controller;
  self.status = [[UITextView alloc] initWithFrame:self.window.bounds];
  self.status.editable = NO;
  self.status.font = [UIFont systemFontOfSize:17];
  self.status.text = @"libble 射频测试\n仅 BLE-TEST 合成数据\n";
  [controller.view addSubview:self.status];
  [self.window makeKeyAndVisible];
  NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
  NSString *role = @"advertise";
  NSString *round = @"20261010-r1";
  int seconds = 90;
  for (NSUInteger i = 1; i + 1 < args.count; i++) {
    if ([args[i] isEqualToString:@"--role"])
      role = args[++i];
    else if ([args[i] isEqualToString:@"--round"])
      round = args[++i];
    else if ([args[i] isEqualToString:@"--seconds"])
      seconds = [args[++i] intValue];
  }
  seconds = std::max(5, std::min(seconds, 180));
  __weak BleLabDelegate *weak = self;
  fixture = std::make_unique<blelab::Fixture>(
      "ios", round.UTF8String, [weak](const std::string &line) {
        std::puts(line.c_str());
        std::fflush(stdout);
        BleLabDelegate *owner = weak;
        if (owner) {
          NSString *next = [NSString stringWithUTF8String:line.c_str()];
          owner.status.text =
              [owner.status.text stringByAppendingFormat:@"%@\n", next];
          [owner.status
              scrollRangeToVisible:NSMakeRange(owner.status.text.length, 0)];
        }
      });
  advertising = [role isEqualToString:@"advertise"]; duration = seconds;
  return YES;
}
- (void)applicationDidBecomeActive:(UIApplication *)app {
  if (started || !fixture || app.applicationState != UIApplicationStateActive) return;
  started = true;
  std::puts("RADIO_LAB_FOREGROUND_ACTIVE"); std::fflush(stdout);
  if (fixture->start(advertising)) {
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(duration);
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.01
                                                  target:self
                                                selector:@selector(step)
                                                userInfo:nil
                                                 repeats:YES];
  }
}
- (void)step {
  if (!fixture)
    return;
  fixture->step();
  if (std::chrono::steady_clock::now() >= deadline) {
    [self.timer invalidate];
    fixture->finish();
    fixture.reset();
  }
}
@end
int main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(BleLabDelegate.class));
  }
}
