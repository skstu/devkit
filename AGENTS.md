# devkit

This repository is the authoritative development location for shared native libraries.
Consumers pin a commit and must not edit their dependency checkout in place.
Keep component public headers, existing target names and license notices compatible.
All components use the root VERSION; 0.x minor releases may break source compatibility,
patch releases must preserve it. No cross-toolchain C++ binary ABI is promised.
Keep business identity, pairing, trading state and product policy in consumers.
Build out of tree. Do not access real user identities, credentials or databases in tests.
libengjs and the full libcompr archive backend are retained legacy code, not validated releases.

libflui exposes a versioned C ABI; do not expose Flutter, Objective-C, or C++ types.
Keep its renderer, engine lock and runtime packaging together. Consumers must not
maintain Dart projects for it. Build output stays outside source. macOS is the
initial backend; other platforms require implementation and validation.
