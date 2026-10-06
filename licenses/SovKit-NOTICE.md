# SovKit distribution notices

Copyright 2026 SovKit contributors.

SovKit's original code is licensed under the Apache License, Version 2.0; see
`LICENSE`. This choice was authorized by the project owner on 2026-09-08.
It does not relicense third-party code, vendored components, or their patches.
SovKit names and logos are not licensed as trademarks by this code license.

Official website: https://skstu.com/

SDK builds include third-party components. Their license texts are staged in
`artifacts/sdk/licenses/`; preserve these notices when packaging the runtime.
Dependency versions and the pinned vcpkg baseline are recorded in the SDK
manifest. Do not infer dependency licensing from SovKit's Apache-2.0 license.

The runtime also links the vendored libuvbrg (MIT, copyright 2026 memade)
and uses nlohmann JSON. Their verbatim notices are included as `libuvbrg.txt`
and `nlohmann-json.txt` in SDK licenses and Flutter license assets.

## SQLCipher

The encrypted business store links SQLCipher Community Edition, copyright
2008-2026 ZETETIC, LLC, under its BSD-style license. Its verbatim notice is
included as `sqlcipher.txt` in SDK licenses and Flutter license assets.
The version-pinned build recipe is in `cmake/vcpkg/ports/sqlcipher/`; upstream
source: https://github.com/sqlcipher/sqlcipher . The private `3rdparty/libdb`
static library supplies generic statement and transaction helpers; the SDK
owns the business store and its SQL. No SQLite ABI is exported
by SovKit. Preserve SQLCipher's own license and attribution on redistribution.

## libjuice source availability

libjuice is distributed under MPL-2.0. Upstream source is available at
https://github.com/paullouisageneau/libjuice . The currently pinned build uses
v1.7.2: https://github.com/paullouisageneau/libjuice/tree/v1.7.2 .

The dependency build recipe and any vcpkg patches must be preserved with each
official SDK release: obtain `ports/libjuice` from the vcpkg baseline recorded
in that release's manifest, and include any additional local modifications.
The current baseline is `45f9f39362a4c52e2b1fbe57b7e649db7f3d96d4` at
https://github.com/microsoft/vcpkg/tree/45f9f39362a4c52e2b1fbe57b7e649db7f3d96d4/ports/libjuice .

SovKit's additional covered-source changes and exact build recipe are included
in SDK preview packages at `third_party/libjuice-overlay/`, and in Flutter
bundles at `flutter_assets/assets/third_party/libjuice-overlay/`. APKs are ZIP
archives; their Flutter assets are under `assets/flutter_assets/`. Use the
recipe's pinned upstream version and hash, then apply its listed patches and
copied `.inc` files. Preserve this directory when redistributing a modified
SovKit runtime; the original upstream code and our modifications retain MPL-2.0.

An official release must verify that recipients can obtain the corresponding
covered source and notices. This development notice alone does not certify
that a particular distribution meets all obligations.
