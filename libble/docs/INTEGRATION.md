# libble SDK integration

Import `devkitBle` using the unpacked SDK directory as `CMAKE_PREFIX_PATH`.
Link `devkit::ble` and call `devkit_ble_bundle(target)` to copy the runtime next
to the consumer. The installed example builds as C without a Swift compiler.
macOS app bundles must place/sign the runtime in their appropriate runtime
location and set their loader search path. Release signing belongs to the host.

## Lifetime and events

1. On the Apple main thread, fill `dkble_config` with three distinct canonical
   service/receive/notify UUIDs and an optional wake callback, then create.
2. Configure the host's Bluetooth usage description and sandbox entitlement
   as applicable. Run the main event loop. Explicit start selects scan or
   advertise; only this action constructs managers and may ask permission.
3. The wake callback only schedules a main-thread `dkble_dispatch`; it must
   not reenter the SDK. Dispatch callbacks may connect/send/stop but cannot
   recursively dispatch or destroy. Copy any event bytes retained by the host.
4. Stop/destroy on the main thread. Stop discards old queued events and pending
   completion notifications; all old links and sends are invalid. No callbacks
   occur after destroy returns. The host must not use a released handle.

The query functions `dkble_version` and `dkble_abi_version` are thread-independent.
No permanent background thread or idle polling timer is created by this SDK.
Active connection/send deadlines and explicitly requested reconnects use the
Apple main queue. Explicit `recover` refreshes scanning so duplicate suppression cannot leave
the consumer with only stale candidates; it preserves existing GATT links and
probe authentication requirements. Background discovery/recovery remains subject to OS policy.

## Byte transport and identity

Scan/connect and advertising/GATT are two separate roles. `link` only means
GATT is ready. It does not prove identity or user confirmation. Peer UUIDs and
optional discovery hints are transient transport identifiers. Never persist
these identifiers as a user's identity or mark a peer trusted because of RSSI,
matching advertisement bytes, subscription or successful ATT acknowledgement.

The engine preserves the extracted profile: up to four links, up to 64 cached
candidates, one copied pending send per link (1..65560 bytes), 20s connect
and 25s send deadlines, 3s retries for explicit persistent connect intent.
With-response writes are limited by both write modes and 512 bytes. Notifications
respect the subscriber limit and wait for `isReadyToUpdateSubscribers` on pressure.
Inbound bytes are delivered as chunks, without an application frame parser.

A SEND_COMPLETE success means an ATT response / local notification enqueue,
not an application ACK. Caller-provided request IDs correlate these events.
Busy sends are rejected without replacing accepted data. An inbound central
cannot be physically disconnected individually through CoreBluetooth; the
engine rejects it until unsubscribe while retaining other central connections.

One-shot probes never enter persistent retry until `adopt_probe`. The host must
perform identity authentication BEFORE adopting. Adoption refuses to displace
an already-ready old link. Cancellation removes probe retry state. Optional
8-byte advertisement-hint reassociation is OFF by default (`DKBLE_HINT_REBIND`);
it is only a way to find a new address, never authorization to pair it.

The event queue holds at most 256 events/256 KiB; repeated candidate updates
coalesce. Overflow stops the transport, discards the generation and delivers
ERROR/OVERFLOW. Explicitly start again after invalidating consumer routes.

## Current platform scope

macOS arm64 / macOS 13+ uses a dylib. iOS arm64 / iOS 15+ now builds an
unsigned `DevkitBle.framework`, packaged by `tools/package_ios_sdk.py libble`.
Embed it as a sibling in the application's Frameworks directory and sign it
with the host team. The consumer needs only the C header and this framework;
Swift/CoreBluetooth implementation and build tools remain producer-owned.
Supply `NSBluetoothAlwaysUsageDescription`; add Bluetooth background modes only
if the product explicitly uses those roles. Simulator/XCFramework distribution
has not been validated. See consumer evidence for actual device test coverage. Android/Windows/Linux remain
unimplemented SDK backends; unsupported targets fail configuration explicitly.
Do not replace the user's previously working cross-platform release with this
Apple development preview.
