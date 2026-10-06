#include <libsys/system.h>
#import <Foundation/Foundation.h>

namespace libsys_detail {
stl::path AppleApplicationSupportDirectory() {
  @autoreleasepool {
    // Foundation resolves the actual sandbox/container of this process. HOME
    // and passwd are not equivalent for signed/sandboxed Apple applications.
    NSURL* url = [[NSFileManager defaultManager]
        URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    if (!url || !url.isFileURL) return {};
    const char* value = url.URLByResolvingSymlinksInPath.fileSystemRepresentation;
    return value ? stl::path(value) : stl::path{};
  }
}
} // namespace libsys_detail
