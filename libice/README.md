# libice

Standalone C ABI ICE SDK, with private statically linked libjuice 1.7.2
(devkit overlay 13), libuv and OpenSSL crypto. No default public server,
product account, identity, signing, invitation or message protocol is exposed.
It reuses the current `libnet::IceAgent` implementation as an internal provider;
consumer projects only receive `libice/ice.h` and `libdevkit_ice`.

Current package: macOS arm64, macOS 13.0 minimum, development preview. Other
platforms, real WAN/NAT environments and Sovkit integration await validation.
This is a transport SDK, not a complete P2P application or WebRTC stack.
See [integration and scope](docs/INTEGRATION.md).

## Consume the binary SDK

Unpack the matching platform SDK, then build the included pure-C example:

```sh
cmake -S "$SDK/share/libice/examples/consumer" -B consumer-build \
  -DCMAKE_PREFIX_PATH="$SDK"
cmake --build consumer-build
./consumer-build/ice_consumer
```

Applications include `<libice/ice.h>`, link the runtime (CMake target
`devkit::ice`), and deploy it alongside the application. The example accesses
only loopback; it is a packaging check, not a product demo. The SDK
provides no product identity, credentials, protocol or UI.

## Build and package in devkit

Configure this component out of tree in Release with `BUILD_TESTING=ON`,
`CMAKE_OSX_ARCHITECTURES=arm64` and `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`.
Use the repository vcpkg configuration and pinned libjuice overlay; providers must be static.
Run `python3 tools/package_sdk.py --build <build-directory>` in this component
with `--juice-source <original-v1.7.2-tar.gz>` (SHA-512 is verified).
An uncommitted preview additionally requires `--allow-dirty`. Packaging runs
component checks and verifies the C export allowlist, architecture, minimum OS,
private runtime dependencies and input hashes. Output includes headers, runtime,
CMake integration, documentation, example, licenses and `manifest.json`.
The libice package also includes corresponding patched libjuice source.
These local previews are not release-signed or notarized.
