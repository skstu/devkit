# Third-party integration

libflui is a standalone binary UI SDK maintained in devkit. Your application owns
its business model, validation, persistence and networking. libflui owns native
windows, input, layout, rendering and private runtime deployment. No tdbrg, trading
SDK, libwxui, Flutter SDK, Dart source tree or devkit checkout is needed to consume
the installed package. The examples are offline and contain no account credentials.

## Supported package

The initial package is **0.1.1 preview / ABI 1 / macOS arm64 / Release AOT**.
Only this platform has been validated. The build deployment target is macOS 12,
but that is not evidence of testing on macOS 12; current validation used macOS 26.5.
Windows, Linux, Intel-only and universal packages are not released. Public Windows
export macros reserve a future ABI spelling; they do not implement that backend.

Install Xcode Command Line Tools, CMake 3.24+ and a C/C++ compiler. Ninja is optional.
C users include `libflui/flui.h` (and `desktop.h` for retained controls). The sample
uses C++17; the optional `flui.hpp` convenience layer requires C++20. Its objects
are compiled into your application and never cross the dynamic-library boundary.

## Start with the installed example

Verify the archive with its `.sha256` sidecar and extract it into any local SDK
directory. Preserve symlinks. These commands run from your own project directory;
replace the SDK path with the extracted prefix:

```sh
cmake -S /path/to/libflui-sdk/share/libflui/examples/quotes -B build/quotes \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/libflui-sdk
cmake --build build/quotes
./build/quotes/flui_quotes.app/Contents/MacOS/flui_quotes --self-test
open build/quotes/flui_quotes.app
```

The sample shows a quote list, quantity input, button action and simulated result.
`--self-test` checks engine startup, document acceptance, state updates and shutdown;
it does not simulate a physical click. `controller.hpp` is ordinary application
code; `flui_main.cpp` demonstrates ABI callbacks without Flutter or C++ UI headers.
The optional wxui comparison is a producer-only target: leave
`LIBFLUI_BUILD_WXUI_SAMPLE` off in an installed SDK consumer.

## Add libflui to an application

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_app LANGUAGES C CXX)
find_package(libflui 0.1.1 CONFIG REQUIRED)
add_executable(my_app MACOSX_BUNDLE main.cpp)
target_compile_features(my_app PRIVATE cxx_std_17)
target_link_libraries(my_app PRIVATE devkit::libflui)
libflui_bundle_runtime(my_app)
```

Set `CMAKE_PREFIX_PATH` to the SDK prefix. Use C++20 only if including `flui.hpp`.
The helper installs the required private runtime beside the executable and applies
the SDK's tested renderer settings while preserving other application metadata.
It does not sign or notarize the completed host app. Run it before your normal
application signing step. Do not copy only the dylib: engine, AOT program and
resources form one deployment unit. Move the whole `.app` when distributing it.
After an SDK upgrade, rebuild/relink the app so the post-build copy runs.

For non-CMake integrations, the equivalent deployment layout is documented in
[DISTRIBUTION.md](DISTRIBUTION.md). Keep that process in your build tooling; do not
make each business module manage Flutter details.

## ABI ownership and lifecycle

1. On the OS main thread, fill `flui_window_options` with its `struct_size` and
   `FLUI_ABI_VERSION`, callback and application-owned context. Call
   `flui_window_create`; it returns a hidden window and copies input strings.
2. Run the native event loop (`flui_app_run` for a standalone application). Wait for
   `FLUI_EVENT_READY`, then load XML. A successful submission is queued; wait for
   its `FLUI_EVENT_COMPLETE` before relying on document acceptance.
3. Publish JSON view state and show the window. Correlate replies by request ID.
   Callback strings are borrowed only for the callback duration; copy what you keep.
4. Route action names and values to your controller, then publish new view state.
   IDs, quantities and exact decimal values should remain strings throughout.
5. On CLOSED, stop producers and quit the standalone loop. After it returns,
   destroy remaining window handles and release callback contexts. Stop independent
   timers explicitly. Do not unload libflui after Flutter has started.

Callbacks must not throw. Destroying a window or starting a nested loop from an
event callback returns BUSY; defer that work with `flui_dispatch`. This dispatcher
is thread-safe, but its context must remain alive until the callback runs, even
if a window has closed. Other UI calls belong on the main thread. A native host
may supply its existing event loop instead of calling `flui_app_run`.

| Result/event | Application response |
| --- | --- |
| `FLUI_OK` on submission | Queued, not necessarily parsed or presented |
| COMPLETE with `FLUI_OK` | Document/state accepted; not a GPU presentation fence |
| `FLUI_BUSY` | Coalesce view state and retry later; never replay business actions |
| `FLUI_NOT_READY` | Wait for READY |
| `FLUI_WRONG_THREAD` | Dispatch to the main thread |
| `FLUI_DOCUMENT_ERROR` | Correct the document; last accepted state remains intact |
| `FLUI_ABI_MISMATCH` | Use the matching header/library/runtime set |
| `FLUI_INVALID_HANDLE` | Retire the stale handle; do not send more work |

Limits: eight windows (one engine per window), 1 MiB per document request,
16 requests / 2 MiB pending per window. High-frequency models should retain only
the latest pending view state. User commands must use a separate reliable business
path. Library queue acceptance is never confirmation of a business operation.

## Two document APIs

The small sample uses XML bindings (`flui_window_load_xml`, `flui_window_set_state`).
Its validated vocabulary and binding rules are listed in [README.md](../README.md).
The retained desktop API uses `flui_window_set_tree` / `flui_window_patch`, usually
through `flui.hpp`. These are separate document formats; choose one per window.

Retained trees have `{id, tag, attrs, children}`; IDs and attribute values are
strings. Keep IDs stable. A patch array of `{id, attrs}` **replaces the full
attribute map** for each listed control, rather than merging individual keys.
A whole patch is validated before application. The C++ layer maintains this map
and batches changes for you. Direct C consumers can use the shipped [Retained API reference](RETAINED.md);
it documents a complete form, patch semantics and action payloads without requiring
renderer source. Advanced retained schemas remain preview APIs.

Use measured `DesktopWindow::OnLayout` geometry for paging and sizing. READY and
queue-idle do not mean layout or display completed. Theme names `neutral`, `xp-blue`
and `classic-2003` provide semantic defaults and token overrides; they are not full
Windows emulation. Business controllers must not depend on theme implementation.

## Troubleshooting and current limits

- Missing `@rpath/libflui.1.dylib` or engine: deploy the complete bundle with the
  CMake helper and check that no files were stripped from `Contents/Frameworks`.
- ABI/version mismatch: replace the complete SDK and rebuild, not individual files.
- No UI updates: run the main event loop, handle READY/COMPLETE and inspect statuses.
- Editing/high-frequency updates: keep control IDs stable and avoid replacing an
  editor with another control. Let focused editors retain their in-progress text.
- Fast teardown may print Flutter's `Communicating on a dead channel` diagnostic;
  existing lifecycle tests exit normally, but the warning remains unresolved.

Physical accessibility, native file-panel automation, older OS versions and long
running production stability are not certified. The SDK does not promise lower
CPU than every native toolkit; compare your own workload at equal scale and cadence.
