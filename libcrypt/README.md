# libcrypt SDK

libcrypt is devkit's independently distributable cryptography SDK. Consumers use
`include/libcrypt/crypt.h` and one shared library through C ABI version 1.
The optional `crypt.hpp` convenience layer is header-only C++20. Neither header
exposes libsodium, OpenSSL, STL objects across the ABI, or Sovkit business rules.

The binary is named **devkit_crypt**, avoiding the operating system's libcrypt.
libsodium and OpenSSL Crypto are linked privately and statically. Distribute the
SDK licenses and the runtime together. This is not a FIPS validated module.

## Integrate

```cmake
find_package(devkitCrypt 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE devkit::crypt)
devkit_crypt_bundle(my_app)
```

Configure with `-DCMAKE_PREFIX_PATH=/path/to/sdk`. Call
`dkcrypt_initialize(DKCRYPT_ABI_VERSION)` before using the SDK. See
[ABI.md](docs/ABI.md), [ALGORITHMS.md](docs/ALGORITHMS.md), and the independent
C-only example in `examples/consumer`. Installed documents are in
`share/libcrypt`, the example in `share/libcrypt/examples/consumer`.

On macOS the app loads `@rpath/libdevkit_crypt.1.dylib`.
The helper copies that runtime next to the target and adds a relative loader
search path. For an app bundle, embed in the bundle's Frameworks directory,
set the corresponding rpath, and sign the embedded library with the app.
Codesigning/notarization remain the distributor's responsibility.

## Build and package

Configure this directory as a standalone CMake project with the devkit/vcpkg
toolchain and **static** libsodium/OpenSSL dependencies, then build and run CTest.
The version comes from devkit's root VERSION. Example configuration:

```sh
cmake -S libcrypt -B .build/crypt-sdk -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx13
cmake --build .build/crypt-sdk
ctest --test-dir .build/crypt-sdk --output-on-failure
python3 libcrypt/tools/package_sdk.py --build .build/crypt-sdk \
  --dependencies /path/to/vcpkg_installed/arm64-osx13 --allow-dirty
```

Use the repository providing `arm64-osx13` as the vcpkg overlay triplet directory,
or supply an equivalent static triplet. With an existing installed tree, pass
`VCPKG_MANIFEST_MODE=OFF` and `VCPKG_INSTALLED_DIR`.
The packager rebuilds, tests, checks exported symbols and dynamic dependencies,
includes provider licenses, and records source, archive and payload SHA-256.
`--allow-dirty` explicitly produces a **development-preview**, never a release.
No package is uploaded automatically.

This phase validates macOS arm64, minimum macOS 13. Other platforms need their own
binary slices, runtime packaging and validation before distribution. C ABI
declarations include Windows calling conventions; this is not a Windows release.
The package tool currently accepts only a native macOS arm64 Release build.

## Ownership

Randomness, hashes, signatures, AEAD, KDF and generic Noise XX belong here.
Identity/trust policy, password envelopes, database contexts, replay policy,
pairing payloads and stream record layouts belong to the consumer.

The old `sovrankit::libcrypt` source target and `libcrypt.h` are retained for
existing pinned source consumers. They are not part of the new binary SDK.
Its frozen Sovkit compatibility sources are not linked into `devkit_crypt`.
New applications must use `devkit::crypt`; do not copy provider sources.
