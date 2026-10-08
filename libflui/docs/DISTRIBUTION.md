# Building and distributing the SDK

## Consumer boundary

The public dependency is libflui's versioned C ABI plus its public headers. The
optional C++ convenience layer contains no exported C++ ABI. Runtime contents are
private implementation details, maintained and upgraded together by devkit.
A third party can build an application from the extracted SDK without devkit,
Flutter, Dart or any business project's source. This is one SDK, not one physical
shared-library file.

```text
libflui-sdk/
  include/libflui/{flui.h,desktop.h,flui.hpp}
  lib/libflui.dylib -> libflui.1.dylib -> libflui.0.1.1.dylib
  lib/libflui_runtime.bundle/Contents/
    Frameworks/{FlutterMacOS.framework,App.framework}
    Resources/{runtime.json,licenses/}
  lib/cmake/libflui/                 # imported target and deployment helper
  share/libflui/{README.md,CHANGELOG.md,LICENSE,flutter.lock.json,docs/,examples/}
  sdk-manifest.json                 # commit, source/runtime identity, file hashes
```

The archive also has a `.sha256` sidecar. Hashes provide integrity and provenance
information, not publisher authentication. Git carries source and documentation;
SDK binaries are separate distribution artifacts, not committed build output.

## Producer requirements and commands

Build on macOS with Xcode Command Line Tools, CMake 3.24+, Python 3.9+ and the
Flutter toolchain exactly matching `flutter.lock.json` (framework, engine, Dart).
Precache macOS engine artifacts and resolve the locked pub dependencies in a
scratch copy of `renderer` before an offline build. The producer deliberately uses
`pub get --offline --enforce-lockfile`; an empty package cache will fail clearly.
Do not substitute another Flutter version or edit generated consumer projects.

From a devkit checkout:

```sh
cmake -S libflui -B .build/flui -DCMAKE_BUILD_TYPE=Release \
  -DLIBFLUI_FLUTTER_SDK=/path/to/locked/flutter \
  -DLIBFLUI_ARCHS=arm64 -DBUILD_TESTING=ON
cmake --build .build/flui
ctest --test-dir .build/flui --output-on-failure
python3 libflui/tools/package_sdk.py --build-dir .build/flui \
  --output-dir .build/packages
```

The packaging tool requires a standalone Release producer configured from this
checkout. It rebuilds libflui, installs into a fresh temporary prefix, verifies
runtime signatures and the engine lock, and writes an archive, checksum and
manifest. It refuses uncommitted component/build-support changes by default;
`--allow-dirty` explicitly marks a local experiment. Unrelated repository changes
are recorded but not packaged. The tool does not run UI tests or certify a release:
run the test command above on a logged-in desktop before packaging.

A package name includes version, preview channel, platform, architecture and short
source commit. For each distributable build, extract the archive into a new path,
build/run the installed quotes example, move the `.app` again and confirm that its
libraries load from its own bundle. Verify signatures after deployment. Keep test
results and the checksum with the distributed artifact. Do not mix an old engine,
new AOT program or headers from another release.

## Manual host deployment and signing

CMake's `libflui_bundle_runtime(app)` performs these steps:

1. Copy the versioned dylib as `My.app/Contents/Frameworks/libflui.1.dylib`.
2. Copy `libflui_runtime.bundle` alongside it, preserving framework symlinks.
3. Add `@executable_path/../Frameworks` to the executable's runtime search paths.
   The dylib finds private frameworks relative to its own location.
4. Apply the locked macOS renderer policy to the app Info.plist. Use the supplied
   `ConfigureApp.cmake`; this setting belongs to the SDK, not application UI code.

Producer frameworks are ad-hoc signed for development. Distribution under your
identity requires signing nested frameworks, the runtime bundle, dylib and outer
app in dependency order, then verifying the whole app. Developer ID signing,
notarization, sandbox entitlements and distribution-channel acceptance have not
been certified by this preview. The SDK does not store signing credentials or
supply an application-specific entitlement policy.

## Versioning and updates

Component versions follow devkit's root VERSION. ABI 1 is a separate number and
uses the `libflui.1.dylib` install name. Patch updates preserve source compatibility;
0.x minor updates may change source-level APIs. CMake version matching is limited
to the same minor version during 0.x, so a 0.1 consumer does not silently select 0.2.
Check `flui_abi_version()` at the boundary; keep header, library and private runtime
from one SDK build. Recompile consumers when upgrading the C++ convenience header.
Advanced retained schemas remain preview APIs, even with an unchanged C ABI.

Changing Flutter requires an explicit lock update, AOT rebuild, renderer/lifecycle
regression, independent consumer relocation and workload measurements. The current
Skia/Metal policy must be re-evaluated with an engine upgrade. Rollback replaces the
whole SDK/app bundle. Do not dynamically unload and replace the engine in-process.

## Notices

libflui-original source is MIT licensed; retain `LICENSE`. Flutter and bundled
upstream components retain their own terms. The package includes Flutter's license,
engine artifact notices and the generated Dart asset license registry inside
App.framework. Preserve these files when redistributing. A package with missing
required license artifacts is rejected by the packaging tool.
