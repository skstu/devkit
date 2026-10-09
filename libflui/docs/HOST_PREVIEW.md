# SDK-owned host preview (unreleased)

This development profile exercises the shared retained renderer on Android, iOS,
Windows and Linux. The independently distributed 0.1.1 SDK remains macOS arm64.
A successful device preview does not release or certify a third-party platform SDK.

## Ownership and integration

The producer owns the Flutter project, Dart renderer, engine, AOT assets and
platform host. Consumer source is C++/XML plus translation catalogs; it links
installed libflui headers and binaries. No consumer Dart implementation is needed.

The public host.h declares the consumer's flui_client_start(ABI version) entry.
Compile the module with LIBFLUI_CLIENT_BUILD and export that entry. The producer
host calls it once on its UI owner thread after attaching the runtime. Return a
flui_status and keep callback contexts alive. The private flui_host_* exports are
an implementation detail, not consumer APIs.

This owner thread is the Flutter UI isolate thread; it need not be the OS main
thread. Create and update controls there; workers use flui_dispatch. The macOS
native-window profile continues to use the OS main thread. Callbacks must not
throw or free contexts that are still queued.

## Capabilities and lifecycle

- One retained surface per process. The host owns its physical window and event
  loop; tree/patch, actions, dispatch, timers and XML-to-control construction work.
- Requests remain bounded to 1 MiB each, 16 pending / 2 MiB per surface. COMPLETE
  confirms document acceptance, not presentation or business-operation success.
- Window title/size/minimum changes, modal windows, app_run, binding XML/state,
  file panels, executable-path lookup, atomic file writes, bell and legacy tick
  are unsupported. They return FLUI_UNSUPPORTED; minimum-size hints are optional
  in the C++ convenience layer.
- Logical close does not close the OS window. app_quit does not terminate the
  host. Process shutdown, resume/background lifecycle and controller teardown
  are not yet a complete public lifecycle contract.
- No hot restart or library unloading. Use this profile for local UI previews,
  not a background service or an application requiring reliable teardown.

The mobile preview supports safe-area insets, narrow navigation, a keyboard-aware
composer and scrollable settings. It does not provide real network, Bluetooth,
identity storage or a native file picker. Zhiyu shows a sample attachment card.

## Producer build

Use tools/build_host_preview.py with --platform android, ios, windows or linux,
--flutter-sdk, --client, --work and --bundle-id. Android also requires
--android-ndk. Consumers can pass `--display-name`, `--chinese-display-name`
and `--logo-dir` for application names and existing Android/iOS/Windows icons;
Linux desktop entry and icon installation remain consumer packaging metadata.
The current harness builds the Zhiyu C++ sample and an SDK-owned
host in the work directory. Consumer CMake receives a binary SDK prefix and an
exact manifest lock; it does not compile Flutter.

The harness requires the public packages in the local cache and uses offline
pub get with the committed dependency lock. Configure mirrors only in the
producer environment when needed; do not rewrite the dependency lock.

All platforms use flutter.lock.json: Flutter 3.47.6, framework
5fc346839b5d0eef006ed8404392afb4dfae428d, engine
692136cb6582dbfc5af3fb33c2515a069f2f66d0, Dart 3.13.5.

Windows uses the official engine DLL and Release AOT. libflui, the C++ consumer
and generated runner use /MT (Debug /MTd). This is static C/C++ runtime linkage,
not a statically linked Flutter engine. Private engine compilation was abandoned
on 2026-10-08 at the user's request; SDK deployment must include the engine,
AOT program and assets. Android native modules use c++_static.

The native SDK prefix is a development linking input, not a complete platform
runtime distribution. Distribute the whole generated preview output when testing;
copying flui.dll alone is insufficient. Independent SDK packaging, deployment
helpers, signing and lifecycle acceptance remain release work.

## Linux producer baseline

The Linux x86_64 profile requires an Ubuntu 22.04 userland. A Debian build host
can run that userland in a container; compiling directly against Debian 12 does
not establish Ubuntu 22.04 compatibility. The producer verifies `/etc/os-release`
and rejects another baseline. Use the locked Flutter revision and CMake >= 3.24,
Clang, Ninja, pkg-config, GCC 11 C++ development files and GTK 3 development files.

```sh
python3 libflui/tools/build_host_preview.py --platform linux \
  --flutter-sdk /path/to/flutter --client /path/to/consumer \
  --work /path/to/build --bundle-id com.example.ui.preview
```

The complete output is `build/host/build/linux/x64/release/bundle`. Ship that
directory, including `lib` and `data`. Native bridge, controller and runner link
the C++ runtime statically; the official Flutter engine, GTK, glibc and graphics
drivers retain their normal dynamic dependencies. Libraries load relative to the
executable, so a relocated application does not depend on the producer directory.

The consumer owns its application package metadata. Zhiyu's Linux packager checks
all five shipped ELF binaries for missing dependencies, relative runtime paths
and symbol requirements no newer than GLIBC 2.35 / GLIBCXX 3.4.29 / CXXABI 1.3.13.
A baseline and ABI check do not replace real input-method, compositor, GPU,
accessibility or future distribution validation.

For a single-UID rootless build container, use `TAR_OPTIONS=--no-same-owner` when
running Flutter: upstream tool archives can contain unmapped owner IDs. This is
a producer environment setting, not a consumer runtime setting.

## Evidence (2026-10-08)

| Target | Evidence | Remaining boundary |
| --- | --- | --- |
| macOS arm64 | Packaged AOT SDK, model/renderer checks and actual Chinese typing | Production reliability and distribution signing |
| Pixel 7 Pro / Android 17 | Release arm64 APK installed under independent UI-preview identity; user confirmed display, typing/sending and locale switching | Background/resume, accessibility and release signing |
| iPhone SE / iOS 15.8.8 | Release arm64 app installed under existing test identity; user confirmed display, input and locale switching | Lifecycle, accessibility and App Store distribution |
| Windows x64 | Release host, bridge and C++ consumer compiled; user confirmed display, Chinese typing/sending, three languages and dark/Classic 2000 themes | Standalone SDK distribution and lifecycle acceptance |
| Linux x86_64 | Ubuntu 22.04 Release build, ABI/bridge checks and real AOT UI self-test; same binaries passed on Debian 12.12 desktop; user confirmed Chinese typing/sending, three languages and dark/Classic 2000 themes | Independent Ubuntu desktop, standalone SDK distribution and lifecycle acceptance |

Device confirmation is distinct from widget tests. System locale/appearance
change tests are simulated; the user's system settings are not changed for tests.
