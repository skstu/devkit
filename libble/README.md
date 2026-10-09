# libble

Independent BLE byte transport with the stable C ABI in `libble/ble.h` and one
private `libdevkit_ble` runtime. Service/RX/TX UUIDs belong to the caller. The SDK
contains no Flutter, Sovkit identity, pairing protocol, message store or product
service UUID. Authentication, encryption, application framing and delivery ACKs
belong to the consumer.

Backends now exist for macOS/iOS (CoreBluetooth), Windows (WinRT), Android
(JNI and SDK-owned Kotlin) and Linux (GIO/BlueZ). These remain development
previews. Build results and synthetic radio evidence are distinct from product
acceptance, background support and release signing. See
[radio validation](docs/RADIO_VALIDATION_20261010.md) for the exact tested matrix
and unresolved cases.

| Platform | Producer requirements | Consumer requirements |
| --- | --- | --- |
| macOS arm64 / 13+ | Apple SDK and Swift | Public C header, dylib, privacy declarations and main loop |
| iOS arm64 / 15+ | Apple SDK and Swift | Header, embedded/signed DevkitBle.framework and privacy declarations |
| Windows x64 | MSVC and Windows SDK with C++/WinRT | Header/DLL, owner-thread dispatch and supported Bluetooth adapter |
| Android arm64 / API 24+ build baseline | NDK and Kotlin Android build | Header/SO, installed SDK Kotlin sources, application Context and runtime permissions |
| Linux x64 / Ubuntu 22.04 build baseline | C++20, GIO development package, BlueZ API | Header/SO, system GIO and a powered BlueZ adapter with required roles |

The exercised Windows host is Windows 11, Android host is Android 17 and Linux
host is Debian 12 with BlueZ 5.66. Other OS versions, architectures, hardware,
background transitions and simulator/XCFramework distribution need separate
validation. Debian binaries are built against the Ubuntu 22.04 baseline.

```sh
cmake -S libble -B .build/ble-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build .build/ble-sdk
ctest --test-dir .build/ble-sdk --output-on-failure
cmake --install .build/ble-sdk --prefix /path/to/ble-sdk
```

For Apple builds use Ninja, arm64 and the matching deployment target (macOS
13.0 or iOS 15.0). Android uses the NDK toolchain and `BUILD_TESTING=OFF` for
cross-compilation. Windows uses the Release configuration. Ordinary CTest
never starts a radio, connects a device or reads a real identity.

Consumers link CMake target `devkit::ble`; `devkit_ble_bundle(target)` deploys
the private runtime beside a desktop consumer. The included pure-C desktop
example checks installed files without starting Bluetooth. Android must first
initialize `com.skstu.devkit.ble.BleRuntime` with application Context. Consumer
signing, permissions and foreground/background policy remain host-owned.

macOS integrity packaging: `python3 libble/tools/package_sdk.py --build <build>`.
Uncommitted development inputs require `--allow-dirty`. iOS packaging:
`python3 tools/package_ios_sdk.py libble --build <build> --output <directory>`.
Other platforms currently provide CMake installation; no verified release
archive, notarization, Play/App Store or universal OS support is implied.
See [integration](docs/INTEGRATION.md) and historical [extraction](docs/EXTRACTION.md).
