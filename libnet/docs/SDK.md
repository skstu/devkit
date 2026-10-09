# libnet independent SDK

The standalone build is `libnet/sdk`. Consumers include `libnet/net.h`, link
`devkit::net`, and ship the private runtime using `devkit_net_bundle(target)`.
No provider headers or development packages are needed in a consumer.
The original context provides a dual-stack UDP listener, bounded UDP sends, wake
slots, timers and framed QUIC records/streams. The additive LAN API provides
independent UDP sockets, IPv4 broadcast, IPv4/IPv6 multicast, interface address
snapshots and configurable change watching through the same public C header.
Product discovery payloads, identities and trust stay in consumers. ICE, TCP
and HTTP remain in source targets pending their separate migrations. The old target
names and default legacy QUIC ALPN remain source-compatible.

ABI 1 exposes only C types. Structures have explicit sizes. Handles are opaque;
all operations except `dknet_wake` are confined to the creating thread. Run
blocks on events. Shutdown stops callbacks and closes handles; destroy drains
pending closes. Stop/join external wake producers before destroying a handle.
Callbacks borrow bytes only until return, cannot throw, and cannot destroy/run
the context. A QUIC custom sender may call send_datagram but cannot reenter
QUIC. Dispatch callbacks may enqueue records; recursive dispatch and shutdown
inside a dispatch callback are rejected. Schedule shutdown through a wake.

Create requires explicit ALPN, queue limits and connection limits. QUIC uses an
ephemeral certificate, with no hostname/trust-chain authentication and no 0-RTT.
It is a byte bearer for applications that authenticate their own protocol, such
as Sovkit's Noise binding; it is not a replacement for authenticated HTTPS.
QUIC READY does not grant trust. Send acceptance does not mean peer receipt or
durable storage. Consumers retain protocol schemas, identities, authorization,
retry policy and delivery acknowledgments. The SDK never selects a relay.

Call `dknet_quic_dispatch` after operations and QUIC_TICK/QUIC_EVENTS to drain events.
QUIC_EVENTS uses a coalesced wake, so final close events arrive even when the
last connection has stopped the periodic timer. The
provider's periodic timer runs only while connections exist. Wake slots coalesce
without allocating an unbounded command queue. UDP returns BUSY at its configured
limit and emits WRITABLE when a send completes after backpressure. Applications
must handle IO errors and preserve their own reliable-message state.

Validated package scope remains separate from source portability: macOS arm64
/macOS 13+ and iOS arm64/iOS 15+ builds exist; the LAN extension additionally
passed isolated Windows full-SDK tests and Ubuntu 22.04 LAN-module tests. This
does not assert five-platform product or background acceptance. See LAN.md. libuv, ngtcp2 and OpenSSL are
statically linked and hidden; only dknet_* symbols are exported. Third-party
licenses, exact source/provider hashes and the runtime accompany SDK packages.


## LAN API v1

Include only `libnet/net.h` and query `dknet_lan_version()`. The original ABI-1
config/event layouts and all 28 original symbols remain unchanged; LAN adds
23 symbols. `dknet_lan_create` has its own opaque owner-thread context and loop;
it requires no QUIC listener, ALPN, product protocol or default service UUID.
The pure-C installed example `net_lan_consumer` exercises actual loopback UDP.
Full semantics and validation are in [LAN.md](LAN.md).

A LAN context bounds all copied sends together by both datagram count and bytes,
socket count (including draining failed/closed sockets), and per-socket group
memberships. All limits are explicit caller inputs within hard caps. Each UDP
socket has a fixed receive buffer; truncated packets are dropped with an error,
never delivered as complete data. Receive start/stop supplies host backpressure.
There is no retained receive/event queue. The caller copies callback data if needed.

Socket close rejects new sends and suppresses datagrams. Existing sends finish
or cancel with SENT, then CLOSED confirms native close. A new socket ID is never
reused. Rebind rejects sends while replacing the descriptor, retains the port,
receive state, multicast interface, TTL/loop/broadcast flags and group memberships,
and emits REBOUND only with the actual restoration result. Failure closes the
socket; it never silently selects a different interface or drops a membership.
Context shutdown immediately suppresses user callbacks, cancels pending work
and closes all owned handles. Destroy drains callbacks internally and frees the
context; stop/join cross-thread wake producers before destruction.

`dknet_interface_list` enumerates assigned address records (IPv4/IPv6, index,
scope, netmask, derived IPv4 broadcast and internal/link-local flags). It is not
an inventory of all down physical adapters or a reachability test. A too-small
output is left untouched and reports required count, with a hard cap of 1024.
`dknet_lan_watch_interfaces` polls at an explicit 250..60000 ms interval; 0 stops
automatic polling. Refresh requests coalesce. A successful initial/changed
snapshot emits one generation; unchanged results emit nothing. Poll failure
retains the last good snapshot and emits ERROR. Hosts may request refresh after
native network notifications. The SDK does not choose routes or auto-rebind.

Windows consumers must ship the same SDK runtime and import library. Linux
consumers retain their required runtime baseline; this extension was tested in
an Ubuntu 22.04.5 rootfs, not built against Debian's newer userspace. Android
permissions and mobile foreground/background policy remain host responsibilities.
