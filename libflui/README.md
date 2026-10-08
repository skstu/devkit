# libflui

Experimental C ABI UI SDK. Authoritative source lives in devkit; the Flutter
renderer, engine version and runtime deployment are maintained here. Consumers
include `<libflui/flui.h>` and link `devkit::libflui`. They do not build Dart or
include Flutter headers. The first backend is macOS; other platforms are not
implemented. No business application or trading SDK is a dependency.

For third parties, start with [Integration](docs/INTEGRATION.md). Maintainers and
SDK distributors should read [Distribution](docs/DISTRIBUTION.md). See
[Changelog](CHANGELOG.md) for the preview scope and limitations.

## Build the SDK (maintainers only)

Supply a Flutter SDK matching `flutter.lock.json`; the build copies the renderer
into the build tree, resolves the locked packages offline, and AOT compiles a
Release runtime. No Flutter-generated files are written to the source tree.

```sh
cmake -S libflui -B .build/flui -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLIBFLUI_FLUTTER_SDK=/path/to/flutter
cmake --build .build/flui
ctest --test-dir .build/flui --output-on-failure
cmake --install .build/flui --prefix .build/flui-sdk
# From a committed component, produce a checksummed SDK archive:
python3 libflui/tools/package_sdk.py --build-dir .build/flui --output-dir .build/packages
```

Optionally set `LIBFLUI_BUILD_WXUI_SAMPLE=ON` with the normal libwxui dependency
prefix to build both adapters around `examples/quotes/controller.hpp`.
No business or trading SDK is linked; quotes and submissions are entirely local.

## Consume the binary SDK

```cmake
find_package(libflui CONFIG REQUIRED)
add_executable(my_app MACOSX_BUNDLE main.cpp)
target_link_libraries(my_app PRIVATE devkit::libflui)
libflui_bundle_runtime(my_app)
```

Point `CMAKE_PREFIX_PATH` at the installed SDK. The bundling helper copies the
complete runtime into the application. `examples/quotes` is also an independent
consumer CMake project. A C compiler can use the same public header.

The package includes `libflui` plus private engine/AOT frameworks and assets.
These are one versioned deployment unit, **not one physical dylib**. AOT compilation
does not statically link the Flutter engine. Never mix runtime files from different
SDK versions. Application signing/notarization remains the application's release
step; development artifacts use ad-hoc signatures. Flutter and third-party licenses
are retained with the runtime and in its generated asset license registry.

## ABI v1

Opaque numeric window handles, fixed-width integers, sized UTF-8 spans, versioned
options and callbacks. No C++/Dart/Objective-C types cross the ABI. ABI version is
separate from devkit's component version; v1 is initially experimental. Header
comments specify lifetimes and limits. All UI calls/callbacks are on the main
thread; worker threads may use `flui_dispatch` or their host's main-thread dispatcher.

Create a hidden window, run the native event loop, wait for READY, load XML and
send full JSON view-state snapshots. `OK` means queued; COMPLETE reports parsing
and model acceptance, not GPU presentation. Window close emits CLOSED; call quit
if using the optional standalone loop, then destroy after run returns. Destruction
cancels pending replies and timers. Do not destroy or enter a nested run loop from
an event callback; BUSY is returned. Stop producer threads before destroying the
host application. Do not dynamically unload the dylib after starting Flutter.

At most 8 windows, each with one engine; at most 16 requests / 2 MiB pending per
window, 1 MiB per input. Full queues return BUSY without silently dropping input.
A consumer can coalesce state snapshots; user actions are delivered as events and
must not be interpreted as requests to automatically retry business operations.

## XML and state

The initial vocabulary is Window, VerticalLayout, HorizontalLayout, Label, Edit,
Button and List. It deliberately shares familiar libwxui names, not full schema
compatibility. Layout accepts gap/padding; controls accept width/height/name;
layout children may use weight. Labels support text/fontsize/bold; Edit supports
value/hint/enabled/action; Button text/enabled/action. List requires a fixed height,
items binding, optional selected string ID/action and one row template.

`{path.to.value}` binds state; `{item.field}` binds a list row. Strings carry exact
prices, quantities and identifiers. Enabled/bold bindings are booleans. Rows need
unique nonempty string `id` values. List rendering is lazy. State snapshots update
only nodes whose bound values changed; quote updates preserve input focus/cursor.
Focused editors own their in-progress
text, including during delayed state echoes; model text reconciles on blur.
Malformed XML, unknown tags/attributes, DTD/entities, duplicate names/row IDs and
incorrect binding types are rejected. Maximum XML depth 32 and 4096 nodes. Existing
valid document/state survives rejected requests. There are no scripts, network
requests or trading rules in XML. New widgets are implemented once in this SDK.

## Validation status

2026-10-08, macOS arm64: producer Release build, pure-C ABI (real AOT engine),
shared business controller, XML/widget interaction tests and the libwxui adapter
roundtrip passed. An independent C++ consumer built from the installed SDK alone;
its relocated .app loaded libflui, the engine and AOT program exclusively from its
own bundle. Framework signatures were checked after deployment. No Flutter SDK or
devkit source path is exported by the installed CMake targets.

A real Flutter window was exercised: select GOLD, clear/re-enter quantity 2,
click Submit, and observe the shared controller's simulated result. Invalid
quantity rejection was also observed. Native text insertion/backspace works;
the automation tool's AX set-value/select-all operations produced an inconsistent
native accessibility value versus rendered/editor state. Those operations are NOT
certified by this sample; reproducing with physical keyboard input and extending
macOS accessibility/editing coverage remains work before production use.

No cross-platform, production, full accessibility or CPU-reduction claim is made.
The local libwxui sample uses existing cached dependencies built for macOS 26.5;
this run does not establish older macOS compatibility. Existing libwxui remains a
source/static component; this task does not migrate its ABI or packaging.

## Retained desktop controls (Desk integration preview)

`<libflui/desktop.h>` extends the C ABI with a retained document, atomic attribute
patches, a UI-thread dispatcher, native timers, modal/modeless window lifecycle,
filtered file panels, bounded atomic file writes and a bounded XML visitor.
`<libflui/flui.hpp>` is an optional C++20 convenience layer using only the standard
library. It is compiled by the consumer; no C++ objects cross the shared library.
The renderer does not know any product, account, order, FIX or tdbrg type.

The retained protocol is separate from the small binding-based sample above:
`flui_window_set_tree` accepts `{id,tag,attrs,children}`; IDs and all attribute
values are strings. `flui_window_patch` accepts an array of `{id,attrs}` replacing
attributes on existing nodes atomically. Unknown attributes, duplicate IDs,
malformed typed values, excessive depth (64) or nodes (16384) are rejected without
partially applying a document. C++ XML fragments use the same generic vocabulary;
business code can register factories for its own XML tag names.

Controls include containers, weighted rows/columns, labels, decimal labels/buttons,
icons, edits, selectors, checkboxes, tabs, virtual lists, movable/resizable panes and
plots. Stable control IDs retain editors, caret, focus, scroll and row state across
quote updates. One in-flight document update is permitted by the convenience
layer; subsequent changes coalesce until acceptance. User actions never coalesce
into an automatic retry. All interaction and business validation stays with the
consumer. Detached, disabled or invisible controls reject late user events.

Layouts complete asynchronously. `DesktopWindow::OnLayout` reports accepted
geometry changes; use it for page capacity and column sizing. `Ready` means engine
ready; `Idle` means the wrapper queue drained, **not** GPU presentation. Tests of
visible geometry must assert measured rectangles. `EnableFrameProfiling` enables
bounded build/raster/pipeline timing samples; these are Flutter timings, not an
end-to-end market-data latency measurement.

Named themes `neutral`, `xp-blue` and `classic-2003` provide semantic color/font
presets. A Window can override `theme_tokens`; explicit XML styling wins. They are
an extension point for independent consumers, not a full Windows skin or platform
emulation. Pointer/keyboard actions and business controllers are theme-independent.

Native save/open dialogs accept filename extensions. Atomic export accepts up to
64 MiB (including binary NUL); ordinary document messages retain their 1 MiB limit.
Dialogs can be dismissed with Esc; Enter in an edit is an edit event, never an
implicit order. Standalone quit unwinds modal loops before stopping the main loop.
Text-height estimation in the convenience layer is approximate; consumers should
use measured geometry for layouts, not that estimate for pixel comparisons.

This preview migrates the entire Desk UI source while preserving its controller.
Its integration fixture covers quick orders, pending/unknown gates, floating
quotes, tickets, positions/close, pagination/export, product information, date
queries, preferences and large catalogs. It sends no real trading requests.
Physical accessibility and native file-panel automation remain distinct from
these controller and renderer tests. See the consumer's migration report for
measured CPU/memory; this SDK does not promise to outperform libwxui.


The locked macOS SDK currently selects Skia/Metal through its bundling helper.
This is a measured renderer policy, not a lower-resolution mode: logical size,
Retina scale and update cadence are unchanged. The alternative Impeller/Metal SDF
backend was materially more expensive for the Desk fixture on this machine.
The helper preserves the host's other Info.plist values. Re-test this policy when
upgrading the locked engine; do not ask each consuming project to tune Flutter.
Fixed single-line labels update retained paragraphs directly, keeping string
precision and accessibility semantics without rebuilding their widget subtree.

Rapid engine teardown currently prints Flutter's `Communicating on a dead channel`
warning for some built-in channel cleanup. Lifecycle regressions complete and
handles reject later calls; the SDK does not suppress the warning or claim that
upstream diagnostic has been eliminated. Full accessibility and longevity testing
remain release work beyond this local integration preview.
