# libnet LAN C ABI v1

Implementation and validation snapshot: 2026-10-10.

The standalone `libnet/sdk` build now exposes LAN socket and interface primitives
through `libnet/net.h`. It adds 23 functions to the original 28 exports without
changing the original ABI-1 structures, functions, target or SONAME. It does not
implement product discovery, trusted pairing, delivery receipts or route selection.
The original UDP/QUIC context remains available.

## Independent context and lifecycle

Query `dknet_lan_version()`, fill every required limit in `dknet_lan_config`, then
call `dknet_lan_create`. The new opaque context owns its own event loop and does
not require a QUIC listener, ALPN or service UUID. No worker thread is spawned.
All calls except `dknet_lan_wake` belong to the creating thread.

`dknet_lan_run` blocks until shutdown and drains callbacks on that thread. An
idle context still has a wake handle; it does not return just because no sockets
remain. Wake has 32 coalescing slots and timers have eight independent slots;
the event's `request` carries the wake/timer slot. A host can use wake to request
owner-thread shutdown. There is no unbounded cross-thread command queue.

Callbacks borrow datagram data only until return. They must not block or throw,
and recursive run/destroy returns `DKNET_STATE`. Owner-thread operations,
including shutdown, are allowed in a LAN callback. A thrown C++ host callback
is contained, triggers shutdown, and makes run report `DKNET_INTERNAL`.

Shutdown immediately suppresses user callbacks, closes all sockets and cancels
pending work. Destroy drains native closes and frees the context. Both shutdown
and destroy are owner-thread operations. Stop and join every external wake
producer before destroy; a pointer cannot be used after destruction. Wake may
race shutdown while the context is still alive.

## UDP operations and completion states

`dknet_udp_open` binds an explicit numeric IPv4 or IPv6 endpoint; port zero asks
the OS for a temporary port. IPv6 link-local endpoints may include `%scope`.
`dknet_udp_local_endpoint` reports the actual bound endpoint. Handles are
nonzero IDs which are never reused within a context, including failed opens.

The socket config explicitly selects receive capacity, address reuse, IPv6-only
mode, IPv4 broadcast, multicast TTL and multicast loopback. Unknown flags and
cross-family combinations fail. There are no product-specific defaults.

| Operation/event | Meaning |
| --- | --- |
| `dknet_udp_send` returns OK | The SDK copied and queued the datagram. |
| `DKNET_LAN_SENT` | That queued request completed or failed at the OS; `request` is the caller token. This is not peer receipt. |
| Send returns BUSY | The context's count or byte budget is exhausted; the host retains responsibility for retrying. |
| `DKNET_LAN_WRITABLE` | Send credit was released for a live socket that previously saw BUSY; retry may still be BUSY. |
| `DKNET_LAN_DATAGRAM` | One complete received datagram, with sender endpoint and borrowed bytes. |
| `dknet_udp_close` returns OK | Close was accepted, subsequent sends fail and datagram callbacks are suppressed. |
| `DKNET_LAN_CLOSED` | Pending SENT completions/cancellations have finished and native close is confirmed. |
| `dknet_udp_rebind` returns OK | Descriptor replacement was accepted; new sends are rejected until its result. |
| `DKNET_LAN_REBOUND` | Actual replacement result, including option/membership/receive restoration. |

Explicit close emits exactly one CLOSED while the context is live. Repeated
close before confirmation is idempotent. The ID remains inspectable during its
CLOSED callback, then becomes NOT_FOUND. Context shutdown silences all user
events, including SENT/CLOSED/REBOUND, and destroy drains internally.

Rebind keeps the port and configured bind address, broadcast/reuse/IPv6 flags,
TTL, multicast loopback/output interface, memberships and receive state. Pending
sends finish or cancel during replacement. Failed restoration reports IO with
the native diagnostic and closes the socket; it does not report partial success
or silently choose another interface. Close during rebind cancels replacement.
To change addresses after a network change, explicitly close/open sockets using
the new snapshot; rebind is not an automatic routing policy.

`dknet_udp_membership` joins/leaves a numeric same-family multicast group on an
explicit numeric local interface or the OS default. Repeated identical joins
are idempotent; leaving an unknown membership returns NOT_FOUND. Keys are
literal group/interface pairs. Specify a scope for IPv6 discovery (for example
`::%5` as the interface). `dknet_udp_multicast_interface` sets outbound interface
selection. Broadcast requires the explicit IPv4 BROADCAST flag; its destination
is supplied by the host, typically from the enumerated broadcast address.

## Bounds and diagnostics

Required config fields have these implementation caps, not suggested product
defaults. `maximum_memberships` applies per socket. Send limits apply across
the whole context, not separately to each socket.

| Resource | Caller-selected range |
| --- | --- |
| Sockets, including handles draining after failed open/close | 1..64 |
| Retained group memberships per socket | 1..64 |
| Pending copied datagrams across sockets | 1..65536 |
| Pending copied bytes across sockets | 1..64 MiB |
| Fixed receive buffer per socket | 1..65536 bytes |
| Accepted datagram payload | 0..65507 bytes, also subject to send budget |
| Multicast TTL | 1..255 |
| Multicast loopback | 0 or 1 |
| Interface address snapshot | At most 1024 records |

Each socket owns one receive buffer. There is no retained SDK receive/event
queue. `receive_stop`/`receive_start` let the host pause intake; kernel queues
remain OS-controlled and UDP can drop data. A truncated datagram is dropped,
increments the dropped counter and emits BUFFER_TOO_SMALL/UV_EMSGSIZE instead
of a successful partial payload. Copied sends are freed before completion
callbacks can submit replacements. Failed-open handles count against the
socket limit until their native close drains, so repeated bind errors cannot
allocate an unbounded set of handles.

`dknet_udp_statistics` reports per-socket pending counts/bytes and received,
dropped, OS-completed and failed send counts. Events preserve their native
libuv status separately from the stable SDK status; synchronous native failures
can be inspected using `dknet_lan_last_native_error`. This diagnostic is not a
message state or a platform-independent error taxonomy.

Product configuration belongs in the consumer. SovKit should eventually map
its deterministic discovery intervals and budgets from `resources/sovkit.yaml`
into these inputs, while identities, secrets and preferences stay outside that
file. This change does not edit SovKit configuration or dependency locks.

## Interface enumeration and change watching

`dknet_interface_list` provides assigned IPv4/IPv6 address records: name, index,
scope, address, netmask, derived IPv4 broadcast, internal and link-local flags.
It is not an inventory of every down physical adapter, media support, route
availability or Internet reachability. OS enumeration failures are errors;
the SDK never returns a silently truncated snapshot.

Query the count with null output and zero capacity, then allocate and call again.
Addresses can change between those calls: retry BUFFER_TOO_SMALL using the new
required count. An undersized output is left untouched. Output structures are
filled by the SDK; input configs/statistics require their declared struct size
and version. Reserved input fields must be zero.

`dknet_lan_watch_interfaces(interval_ms)` uses portable polling at an explicit
250..60000 ms interval, starting with an immediate poll. Zero stops automatic
watching. `dknet_lan_refresh_interfaces` queues a coalesced refresh, useful after
a host's native network notification. The first successful snapshot and each
changed snapshot emit INTERFACES with a generation; identical snapshots emit
nothing. `dknet_lan_interfaces` copies the latest successful snapshot. A failed
poll keeps that snapshot and emits ERROR. Watching does not rebind sockets,
choose routes or decide whether a discovered peer is trusted.

## Validation evidence and limits

Tests use isolated native contexts, temporary ports and fixed test bytes. They
do not access product identities, keys, contacts or databases. The broadcast
fixture sends one byte to a local interface's derived broadcast address and
requires a broadcast-capable IPv4 address; it fails if no such address exists.

| Environment | Actual scope/result |
| --- | --- |
| macOS arm64, deployment target 13 | Full standalone SDK: four CTest cases passed; ASan/UBSan also passed. |
| Windows x64 | Full standalone SDK with static providers: four CTest cases passed. |
| Ubuntu 22.04.5 rootfs on Debian test host | LAN module built against that userspace with libuv 1.52.1: two LAN cases passed. Full QUIC/provider SDK packaging was not tested here. |
| Pixel 7 Pro, Android arm64/API 24 build | The same LAN module and two native cases passed under adb shell. App permissions/background operation were not tested. |
| iOS arm64, deployment target 15 | Full SDK compiled. No iPhone LAN runtime or background test in this change. |

The new cases cover real IPv4 loopback multicast, membership/output-interface
restoration on rebind, actual local-interface broadcast reception, IPv6 loopback
UDP, copied send ownership, receive truncation/pause, send/socket/membership
bounds, real bind/rebind failure, pending-send close ordering, stale IDs,
shutdown suppression, owner-thread affinity and concurrent coalesced wake.
Actual enumeration/initial watcher notifications run on these hosts; changes
to address, scope, index, flags, mask and removal are additionally exercised
against the snapshot comparison used by the watcher. Real NIC hotplug/address
changes were not induced.

Not yet accepted: physical cross-device LAN discovery/message/file workflows,
IPv6 multicast across actual interfaces, Wi-Fi changes, sleep/wake, mobile
background/permissions, packet-loss pressure, or full Android/Linux SDK
provider packages. Passing a transport fixture does not establish product
delivery, authentication, persistence or restart recovery.

## Consumer and next integration boundary

The installed pure-C `net_lan_consumer` example links only `devkit::net` and uses
`devkit_net_bundle` to ship the runtime. Provider headers remain private. SDK
packaging verifies exact exports and runtime dependencies and records source
and provider hashes. A dirty-source package is a development preview, never
relabelled as a clean release.

The next SovKit integration should consume a verified pinned SDK package, move
its discovery sockets/interface lifecycle onto this ABI, keep existing QUIC and
business authentication/receipts, and then remove replaced legacy discovery
code. This stage does not claim that migration is complete or that a second
product discovery implementation has been introduced in devkit.
