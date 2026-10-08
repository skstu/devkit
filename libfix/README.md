# libfix

C++20 QuickFIX session integration over libuv and verified OpenSSL TLS.
`devkit::libfix` uses the small `libnet_uv` target; it does not pull in HTTP,
ICE, QUIC, proxy bridges, UI, JSON or any trading/product model.

Supply `libfix::Options` with role, address, session IDs, QuickFIX settings,
and reviewed dictionary bytes/paths, then create and start `libfix::Endpoint`.
The same runtime implements initiator and acceptor. FIX::Application stays in
the consumer. EP versions, custom fields, authentication values, subscriptions,
orders and durable business reconciliation are not defined here.

Each endpoint owns one reactor shared by its configured sessions. Application
callbacks, session access and posted work are serialized on that thread.
`post` is bounded and nonblocking; rejection means the work was not queued.
`call` waits for an admitted task (or runs directly on the reactor).
Application, log and store callbacks must return promptly. Destroy the endpoint
from its owning host thread before destroying any referenced callback state.
`stop()` joins the reactor and is idempotent; call it before clearing any pointer
used by callbacks. No callbacks survive stop/destruction. The public C++ source API does not promise
cross-toolchain binary compatibility.

TLS connect, handshake, reads and writes are asynchronous. Peer and aggregate
ciphertext queues, frame size, connection count and posted work are bounded.
Slow peers are closed; accepted writes do not mean remote delivery. Limits and
session/application timer intervals are explicit. TLS 1.2 is the minimum; client
certificate chain and IP/DNS identity verification cannot be disabled.

FileStore uses an exclusive directory lease and caller-supplied dictionaries.
An optional MessageStoreFactory/LogFactory supports custom persistence and
receipt guards. Reset/resend/persist settings remain explicit QuickFIX policy.
Reconnect is opt-in with capped exponential backoff; it does not queue business
commands for replay. QuickFIX's configured resend policy still applies, so
consumers that forbid replay must enforce that in toApp. After reconnect the
application revalidates its own directory, subscriptions and account state.
Metrics expose byte/message counters, queue usage and reconnect/task rejection
counts without collecting message bodies or credentials.

Build the root with `-DNATIVE_LIBS_COMPONENTS=libfix`, or add this directory as a
subdirectory and link `devkit::libfix`. Existing root option names are retained
for source compatibility after the repository rename to devkit.

QuickFIX is fixed at 1.16.0, devkit port revision 3. The existing NonStopSession
constructor patch prevents a continuous session from resetting stored counters
at the UTC day boundary. Use `cmake/vcpkg/ports/quickfix` from this repository;
an unpatched QuickFIX build is rejected. This patch retains its original notice.
