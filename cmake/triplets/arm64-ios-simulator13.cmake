set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_SYSROOT iphonesimulator)
# Apple Silicon simulators first shipped with iOS 14. Keep the historical
# triplet name for compatibility, but encode the first technically valid OS.
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-ios-simulator")
