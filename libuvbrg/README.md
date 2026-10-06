# libuvbrg

This directory contains the portable proxy core vendored from OrbitBridge.
SovKit builds it as an internal static library and does not expose its handles
directly through the public SovKit ABI.

## Dependencies

- libuv
- OpenSSL Crypto
- nlohmann JSON
- C++20 threads

The CMake target is `libuvbrg::libuvbrg`. Dependencies may be supplied by
vcpkg or another CMake-compatible package manager.

## Local changes

- Replaced OrbitBridge-specific CMake variables with standard package targets.
- Excluded the BroSDK reactor parity adapter and legacy `pvb_*` API.
- Disabled unavailable advanced TCP fingerprint fields on Android and iOS.
  General socket buffer fingerprinting remains enabled on those platforms.
- Added an opt-in asynchronous byte-stream upstream (`uvbrg_tunnel_callbacks_t`).
  When absent, the original direct/upstream-proxy paths remain in use. When
  installed before listening, target DNS/TCP is delegated exclusively to the
  host adapter; failure never falls back to direct local egress.
- Added opt-in JSON `strict_proxy` validation and a proxy-only routing guard.

## Strict proxy configuration

`"strict_proxy": true` requires a nonempty `upstreams` array. Every entry must
have type `http` (HTTP CONNECT) or `socks5`, a nonempty host without whitespace or
control characters, and an integer port in 1–65535. Invalid entries reject the
whole configuration, including partially valid chains. Optional `auth` must
specify string `type`, nonempty `user` and string `pass`: `basic` for HTTP,
`socks5` or `userpass` for SOCKS5. SOCKS5 credentials must each fit 1–255 bytes;
HTTP usernames cannot contain `:`. Embedded NULs are rejected. Authentication
and connection failures close the session; they never retry the target directly.
The array remains a sequential proxy chain, not a failover group.

Strict mode rejects nonempty `security.proxyBypassList` / `proxy_bypass_list`
and cannot be combined with a host tunnel adapter. Installing an adapter on a
strict bridge, or adding/updating a strict listener on an adapter bridge, returns
`UV_EINVAL`. The final external HTTP/CONNECT/SOCKS5 route also fails closed if
the effective chain is empty or an adapter conflicts. Internal in-process
routes do not perform external egress and retain their existing behavior.

Invalid initialization returns a null handle; invalid listener updates return
`UV_EINVAL` without changing configuration or closing existing sessions. Missing
`strict_proxy` on an update preserves the current mode. Enabling strict mode
requires supplying the complete upstream chain; an already-strict update may
omit it to retain the validated chain. Non-boolean or duplicate top-level
`strict_proxy` keys are rejected. This is an internal library JSON option;
the public SovKit SDK ABI is unchanged.

The default remains `false`: existing direct routes, proxy bypasses and host
adapters keep their original behavior. An explicit switch to DIRECT is
`{"strict_proxy":false,"upstreams":[]}`. Strict mode does not implement the
Bridge profile importer, proxy-group selection, DNS policy, UDP or transparent
gateway interception.

## SovKit tunnel adapter

The bridge owns its loop and browser sockets. `open` reports a target host/port;
`data` reports borrowed bytes and pauses reads until `UVBRG_TUNNEL_WRITABLE`.
The host must copy those bytes and return promptly. `uvbrg_tunnel_event` may
wait for the bridge loop, so SovKit invokes it from its egress worker, never
from the bridge callback or Flutter UI. Events identify a connection by both
ID and generation; a late event cannot target a recycled connection.

`CONNECTED` finishes SOCKS5/CONNECT negotiation; `DATA` queues bytes to the
browser; `consumed` acknowledges completed socket writes. `EOF` means half-close,
not abort; `CLOSE` aborts one connection. The adapter limits each browser read
to 16 KiB without changing legacy proxy read sizes. SovKit disables fingerprint
and traffic-feature collection and installs no internal web routes.

See [browser network plan](../../docs/tasks/BROWSER_NETWORK_SHARING.md) for
authorization, address policy, quotas, ownership, tests and preview limits.

## Upstream

- Repository: `git@github.com:memade/OrbitBridge.git`
- Path: `3rdparty/libuvbrg`
- Revision: `5384538e159d41c9234e93905849899e216ed7f0`

Review and reapply the local changes above when updating the vendored source.
See `LICENSE` for the upstream license.
