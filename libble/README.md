# libble

Standalone BLE SDK with a versioned C ABI (`libble/ble.h`) and one private
`libdevkit_ble` runtime. No Flutter, Sovkit identity, account, message store or
product UUID is part of the public interface. Internal Apple code is Swift and
CoreBluetooth, extracted from the previously exercised Sovkit Apple bridge.
Consumers need only the installed C header, dynamic library and CMake package.

This is an **Apple backend development preview**, built and checked locally on
macOS arm64, deployment target macOS 13.0. Windows, Android and Linux backends
have not been migrated. iOS shares CoreBluetooth source but is not yet a
validated SDK slice. Neither a successful build nor state fixtures certify
real radio interoperability, background operation or App Store acceptance.
The existing Sovkit product still uses its old bridges until joint integration
with libice. No automatic dependency upgrade is performed by this package.

```sh
cmake -S libble -B .build/ble-sdk -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build .build/ble-sdk
ctest --test-dir .build/ble-sdk --output-on-failure
python3 libble/tools/package_sdk.py --build .build/ble-sdk --allow-dirty
```

The tests never start the radio, open a permission dialog, connect a device,
or read a real identity store. Swift/system frameworks are operating-system
runtime dependencies, not a Flutter SDK requirement for consumers.
See [integration](docs/INTEGRATION.md) and [extraction evidence](docs/EXTRACTION.md).

## Consume the binary SDK

Unpack the matching platform SDK, then build the included pure-C example:

```sh
cmake -S "$SDK/share/libble/examples/consumer" -B consumer-build \
  -DCMAKE_PREFIX_PATH="$SDK"
cmake --build consumer-build
./consumer-build/ble_consumer
```

Applications include `<libble/ble.h>`, link the runtime (CMake target
`devkit::ble`), and deploy it alongside the application. The example accesses
no Bluetooth radio; it is a packaging check, not a product demo. The SDK
provides no product identity, credentials, protocol or UI.

## Build and package in devkit

Configure this component out of tree in Release with `BUILD_TESTING=ON`,
`CMAKE_OSX_ARCHITECTURES=arm64` and `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`.
An Apple SDK and Swift compiler are required to build the SDK itself.
Run `python3 tools/package_sdk.py --build <build-directory>` in this component.
An uncommitted preview additionally requires `--allow-dirty`. Packaging runs
component checks and verifies the C export allowlist, architecture, minimum OS,
private runtime dependencies and input hashes. Output includes headers, runtime,
CMake integration, documentation, example, licenses and `manifest.json`.
These local previews are not release-signed or notarized.

Apple packaging: macOS 13+ arm64 dylib; iOS 15+ arm64 `DevkitBle.framework`.
Use the installed public header and binary; no Swift consumer project is required.
iOS signing and Bluetooth privacy declarations belong to the application.
`tools/package_ios_sdk.py libble --build <build> --output <output>` emits an
integrity manifest and binary lock. This is a development preview; see integration
documentation for platform and validation limits.
