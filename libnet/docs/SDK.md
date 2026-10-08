# libnet independent SDK (first migration slice)

The standalone build is `libnet/sdk`. Consumers include `libnet/net.h`, link
`devkit::net`, and ship the private runtime using `devkit_net_bundle(target)`.
No provider headers or development packages are needed in a consumer.
This slice provides a dual-stack UDP listener, bounded UDP sends, wake slots,
timers and framed QUIC records/streams. ICE, discovery, TCP and HTTP remain in
the existing source targets pending their separate migrations. The old target
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

Only macOS arm64 / macOS 13+ is locally validated in this migration. Other
platform binaries are not delivered or claimed. libuv, ngtcp2 and OpenSSL are
statically linked and hidden; only dknet_* symbols are exported. Third-party
licenses, exact source/provider hashes and the runtime accompany SDK packages.
