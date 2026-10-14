# devkit

**2026-10-14 构建存储约定：固定 Debug／Release 槽位，重复构建覆盖，不保存回滚包。**
Flutter 生产者按消费者、平台、配置复用 `.build/work/libflui/<consumer>/<platform>/<debug|release>/`；
SDK 打包器复用包含组件、平台、架构、配置的固定目录，libflui Mac 归档名固定为
`libflui-macos-arm64-release.tar.gz`，来源指纹与校验继续保留在清单内。
不得复制整套源码或宿主到日期／版本目录，也不得把二进制副本留在交接文档目录。

本机 Flutter SDK、Pub／Dart／Gradle／CocoaPods 缓存、Android SDK／模拟器及临时文件集中在
`/Users/martell/Developer/`；共享缓存为 `/Users/martell/Developer/.cache/flutter/`，旧默认位置只作兼容链接。
生产者在写入前校验 SDK 和输出目录，并显式设置共享缓存；Xcode、Android Studio 沿用系统安装位置。
本轮清理再释放约 22.5 GiB，结束时可用约 141 GiB；历史 Flutter 符号与重复运行库已清理。
必要的 SDK、源码、身份数据与签名证书保留。离线覆盖与入口检查通过，未重建或部署正常 App。
执行约束见 [AGENTS.md](AGENTS.md)；直予三端构建使用相邻 SovKit 仓库的 `scripts/build_zhiyu.py`，详见其 BUILD.md。

Current development handoff (2026-10-11): [source checkpoint, exact packages,
validation and consumer boundaries](docs/HANDOFF_20261011.md).

SKSTU shared C++20 native libraries, extracted from SovKit. Authoritative source:
https://github.com/skstu/devkit . Business projects consume pinned commits.

## Version and compatibility

All components share the root `VERSION` (initial development series **0.1.x**, current **0.1.1**).
`components.json` lists each component's version, status and license. A version
does not certify all platforms or legacy features. During 0.x, minor releases
may change source interfaces; patch releases preserve them. Existing namespaces
and CMake target aliases are retained. Cross-compiler C++ binary ABI is not promised.
Use an exact Git commit for reproducibility, with release tags as human labels.
Do not change published tags. Consumer projects upgrade and validate separately.

## Source integration

After cloning this repository (or adding it as a submodule), select components:

```cmake
set(NATIVE_LIBS_COMPONENTS libpath libstl libcompr)
add_subdirectory(3rdparty)
target_link_libraries(my_app PRIVATE sovrankit::libstl_core
  sovrankit::libpath_utf8 sovrankit::libcompr_stream)
```

Consumers may continue to add individual library directories, retaining existing
paths and target names. Add libpath before libstl when using the full path facade.
No component requires SovKit's SDK source tree. Libraries may require other
components and external dependencies. This release supports source integration;
there is no complete installed `find_package(NativeLibs)` package yet.

## Standalone build

The default selects dependency-light UTF-8/STL/stream headers. To build native
system, crypto and networking components with the pinned vcpkg toolchain:

```sh
cmake -S . -B .build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  '-DNATIVE_LIBS_COMPONENTS=libpath;libstl;libcompr;libsys;libcrypt;libnet;liblog;libdb;libuvbrg' \
  -DLIBSYS_ENABLE_FILE_IO=ON
cmake --build .build/debug
ctest --test-dir .build/debug --output-on-failure
```

`LIBSYS_COMPONENTS_ONLY=OFF` enables the full platform facade.
`LIBPATH_UTF8_ONLY=OFF` enables the Iconv-backed path facade.
`LIBNET_HTTP_ONLY=ON` selects only the HTTP implementation (the initial libnet
vcpkg feature still resolves the complete transport dependency set).
GUI (`libwxui`) and URL helpers (`liburl`) are opt-in. libengjs and full libcompr
compression are retained legacy sources, excluded from the supported root build.
Optional WebSocket backends require additional caller-provided dependencies.

Builds write to .build/, never the source directory. The consuming product owns
its transport policy, identifiers and persistence. Moving network primitives
does not relax Zhiyu's direct-only checks or enable relay experiments.

## Development and provenance

Make fixes here, test and publish a commit, then update consumers' pinned version.
Do not patch business projects' dependency checkouts. AGENTS.md is a workflow
rule, not filesystem access control. Consumers should reject dirty dependencies.

See [original API contracts](docs/FOUNDATION.md), [import provenance](migration/ORIGIN.md)
and [license notices](NOTICE.md). Imported source implementations are unchanged;
the migration adjusts build ownership, version metadata and documentation.

FIX (`libfix`) is opt-in: `NATIVE_LIBS_COMPONENTS=libfix`, target `devkit::libfix`.
It reuses the small `libnet_uv` transport without HTTP/ICE/QUIC dependencies.
See [libfix](libfix/README.md) for threading, TLS, persistence and recovery contracts.
The existing `NATIVE_LIBS_*` options and component aliases remain compatible.

## libflui binary UI SDK

`libflui` is an opt-in experimental component with a C ABI and a private Flutter
renderer. Build/install it independently; consumers use its installed CMake package
and headers without a Flutter toolchain. See [libflui](libflui/README.md).
Its sample shares one business controller with a separate libwxui adapter.

## libnet binary SDK

The first independent networking slice is built from `libnet/sdk` and exports
`libnet/net.h` plus `libdevkit_net` (ABI 1). It contains the event loop, bounded
UDP listener/sends, QUIC records/streams and a local conformance probe. Providers
are statically linked inside the shared library; consumers need no libuv,
ngtcp2 or OpenSSL headers. ICE/TCP/HTTP source targets remain separate for now.
See [the lifecycle and authentication contract](libnet/docs/SDK.md).

```cmake
find_package(devkitNet 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE devkit::net)
devkit_net_bundle(app)
```

Configure `libnet/sdk` using the chosen vcpkg toolchain and static provider
triplet. `libnet/tools/package_sdk.py --build <build> --dependencies <installed-triplet>`
verifies tests, exports, runtime dependencies, licenses and exact file hashes.
The package builder currently validates macOS arm64 / macOS 13+ only. Dirty
source requires `--allow-dirty` and is labeled as a development preview.

## BLE and ICE binary SDK previews

`libble` extracts the existing Apple BLE engine into a C ABI runtime with no
Flutter dependency. `libice` packages the pinned libjuice UDP ICE agent behind
its own C ABI. Both currently have a macOS arm64 development package, pure-C
installed consumer, and exact source/payload provenance. Product integration
and physical BLE/WAN testing are a later joint phase.

See [libble](libble/README.md), [its extraction contract](libble/docs/EXTRACTION.md),
and [libice](libice/README.md). `libsys` remains a static source component.
