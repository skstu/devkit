# libnet

libnet provides portable native networking primitives. The independent C ABI
dynamic SDK is built from `libnet/sdk`; see [SDK scope and contract](docs/SDK.md).

## Capabilities

- Synchronous HTTP `HEAD`, `GET`, parameterized `GET`, `POST`, and `PUT`.
- JSON request helpers and a response-headers callback that can stop the body.
- HTTP diagnostics including curl/OS status and connection-stage timings.
- File downloads with retries, redirects, low-speed protection, cancellation,
  progress callbacks, range probing, parallel parts, and resume support.
- Independent C ABI LAN sockets: bounded UDP, IPv4 broadcast, IPv4/IPv6
  multicast, interface address snapshots and configurable change watching.
- Caller-owned legacy source-target libuv TCP, UDP, and pipe transports.
- A libjuice-backed RFC ICE datagram provider for host/STUN-reflexive/TURN
  relay candidate checks, selected-path diagnostics and bounded event delivery.
- A deterministic transport-provider capability catalog and route selector.
  Non-production providers remain excluded from default policy selection.
- An ngtcp2/OpenSSL QUIC conformance core with TLS 1.3, fixed `sovkit/1` ALPN,
  disabled 0-RTT, a real libuv UDP loopback lifecycle probe, and a long-lived
  shared-port provider with destination-CID demultiplexing and bounded framed
  streams. SovKit exposes it as an authenticated preview, not a production
  route.
- Optional WebSocket client/server backends.

HTTP TLS peer and hostname verification are enabled by default. QUIC here is
an ephemeral, unauthenticated bearer: applications must authenticate their own
protocol before granting access. Its READY event is not an identity assertion.

LAN C ABI contract and validation: [LAN.md](docs/LAN.md).

## CMake

The default target requires CURL and libuv:

```cmake
add_subdirectory(3rdparty/libnet)
target_link_libraries(my_target PRIVATE sovrankit::libnet)
```

Set `LIBNET_ENABLE_WEBSOCKETS=ON` to additionally build the WebSocket backends.
That feature requires uWebSockets, uSockets, and libhv.

Android requires `ANDROID_NDK_HOME` when vcpkg builds native dependencies:

```console
ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/28.2.13676358" \
  "$VCPKG_ROOT/vcpkg" install curl:arm64-android libuv:arm64-android \
    libjuice:arm64-android \
    'ngtcp2[openssl]:arm64-android'
```

Use the repository overlay triplets for iOS so dependencies retain the Flutter
project's iOS 13 deployment baseline instead of inheriting the host SDK version:

```console
"$VCPKG_ROOT/vcpkg" install curl:arm64-ios13 libuv:arm64-ios13 \
  libjuice:arm64-ios13 \
  'ngtcp2[openssl]:arm64-ios13' \
  --overlay-triplets=cmake/triplets
"$VCPKG_ROOT/vcpkg" install \
  curl:arm64-ios-simulator13 libuv:arm64-ios-simulator13 \
  libjuice:arm64-ios-simulator13 \
  'ngtcp2[openssl]:arm64-ios-simulator13' \
  curl:x64-ios-simulator13 libuv:x64-ios-simulator13 \
  libjuice:x64-ios-simulator13 \
  'ngtcp2[openssl]:x64-ios-simulator13' \
  --overlay-triplets=cmake/triplets
```

From the repository root, build and test the full libnet target with:

```console
cmake --preset libnet-debug -S native
cmake --build .build/workspace/libnet-debug --config Debug
ctest --test-dir .build/workspace/libnet-debug -C Debug -R sovkit.libnet --output-on-failure
```

## HTTP example

```cpp
#include <libnet_http_client.h>

const auto response = libnet::HttpClient::PostJson(
    "https://example.test/api", R"({"ready":true})");
if (!response.ok) {
  // response.error contains the curl or HTTP failure.
}
```

## Download example

```cpp
#include <libnet_downloader.h>

libnet::DownloadOptions options;
options.parallelism = 4;
const auto result = libnet::Downloader::Download(
    "https://example.test/archive.zip", "archive.zip", options,
    [](std::uint64_t total, std::uint64_t current) {
      return current <= total; // Return false to cancel.
    });
```

## Provenance

The HTTP and download behavior was adapted from
`OrbitBridge/projects/brosdk/net` at revision
`5384538e159d41c9234e93905849899e216ed7f0`. OrbitBridge-specific URL, path,
logging, signing, and utility dependencies were intentionally replaced with
standard C++ and libcurl APIs. See `LICENSE` for the upstream license.

The ICE implementation links libjuice 1.7.2 through vcpkg. libjuice is licensed
under MPL-2.0; distributed SDK artifacts must retain its license notice and
make any modifications to MPL-covered libjuice source files available under
that license. SovKit currently consumes the unmodified upstream package and
keeps its wrapper in separate SovKit-owned files.
