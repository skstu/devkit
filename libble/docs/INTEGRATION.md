# libble integration

Use only installed public headers, binaries and producer-owned Android sources.
Link `devkit::ble`; never embed consumer-specific identities, storage or service
UUIDs in libble. Existing ABI-1 config/event layouts and original entry points
are unchanged. `dkble_set_options` and `dkble_set_read_interval` are additive.

## Thread and lifecycle contract

Create/use/dispatch/stop/destroy a context on one owner thread; Apple requires
the main thread and a running main loop. The wake callback may run on a native
worker; it only schedules dispatch and must never reenter the SDK. Call dispatch
regularly (at most 100 ms between calls while active), including after wake.
Callbacks may connect, send and stop. Recursive dispatch and destroy during a
callback return BUSY. Event pointers remain valid only during that callback.

Creation does not start scanning, advertising or ask permission. Linux creates
a private worker/context at creation; explicit start opens its own system-bus
connection. Stop invalidates old links, queued data and send completions by
generation, including when shutdown fails. Stop success confirms local native
shutdown; it never fabricates an `off` event after a rejected/failed command.
On failure sending stays disabled; retry stop or destroy. Linux uses a reserved
control slot, clears queued work and fences already-taken batches, then waits
up to 10 s for its worker shutdown barrier. Overflow shutdown failures return
from dispatch and are retried by subsequent dispatch calls. A fresh start also
requires this barrier to succeed. Destroy closes native resources and suppresses late callbacks;
the released handle must never be used again. Linux destroy may wait for owned
GATT disconnect/async cancellation to finish. Do not destroy concurrently.

`dkble_options` carries connect/send deadlines, persistent-connect retry delay,
candidate TTL and maxima (1–4 links, 1–64 candidates). It has its own struct size
and ABI version; set it while stopped. Defaults are 20 s connect, 25 s send,
3 s retry and 30 s inactive-candidate TTL. Active links and persistent intents
are retained. `dkble_set_read_interval` controls idle read polling (10–1000 ms,
default 50 ms). Nonempty read queues drain with one outstanding request, while
writes and reads share the ATT connection. These settings do not grant platform
permissions or override hardware limits.

## Platform setup

Apple: provide Bluetooth usage declarations and sign/embed the matching runtime
with the application's established team. Android: include the installed Kotlin
sources and initialize `BleRuntime.initialize(applicationContext)` before native
context creation. The application requests appropriate scan/connect/advertise
permissions (and location on older Android), owns foreground policy and loads
the SO. libble never silently grants permissions or starts a background service.
Windows: adapter central/peripheral support and radio availability are checked
at explicit start. Linux: system GIO/BlueZ, D-Bus permissions, powered adapter
and GattManager/LEAdvertisingManager roles are needed. Building a preview in a
container does not make its Bluetooth accessible inside that build container;
radio execution uses the desktop host's system bus.

## Roles, routing and byte semantics

Scan/connect and advertise/GATT are separate roles. A link event means the
transport is ready; it proves neither identity nor user confirmation. Peer and
link strings are bounded opaque backend locators (Apple UUID, Android address,
Windows ID, Linux D-Bus object path), never account identities. Only discovery
results are suitable connection targets. Optional eight-byte advertisement
hint reassociation is off by default and only helps locate an address; a hint
never authorizes trust, adoption or pairing.

One-shot probes never join persistent retries before authenticated adoption.
Adoption refuses to replace a ready old route. Cancellation removes probe
intent and queued retry without touching other ready routes. CoreBluetooth
cannot physically disconnect one inbound central: that route is rejected until
unsubscribe while other central connections remain available.

The SDK copies at most one pending send (1–65560 bytes) per link. BUSY preserves
accepted bytes. Incoming events are chunks, without a business parser. The
mailbox is bounded to 256 events/256 KiB, with coalesced discovery updates;
overflow fails closed and requires explicit restart and consumer route reset.
SEND_COMPLETE success means an ATT response, local notification admission or
final device-scoped read response, never application delivery or persistence.
Use authenticated application ACKs for message delivery.

## Linux TX isolation

BlueZ 5.66's external characteristic notification path shares one notification
value/socket among subscribers. A first subscriber's device option does not
turn that socket into a private per-device route. libble therefore exports a
read-only TX characteristic on Linux. `ReadValue` uses BlueZ's device option to
consume only that link's bounded outbound bytes; RX remains with-response write.
There is no shared notification broadcast. New client backends choose notify
when available and otherwise read-only polling, using the same caller UUIDs.
Older notify-only clients reject the Linux profile; do not label them compatible.
The current read profile uses a nine-byte SDK header: kind plus an eight-byte
session epoch. A client first writes HELLO (kind 2); only a matching epoch read
can make its link ready. Server responses use kind 0 for idle or 1 plus payload;
client payload writes use kind 3. Backends validate and strip the header; it
never enters the caller byte stream. Retired epochs cannot reopen a session;
a physical connection has a bounded history of 64 epochs, after which new
sessions are refused until physical disconnect. Old one-byte read clients are
incompatible. This profile requires matching libble clients; it is not an
arbitrary read characteristic. Epochs isolate byte sessions, not identity or
cryptographic authentication.

Read-profile writes use conservative ATT-sized chunks where native maximum-PDU
reporting caused long writes. This favors interoperability over throughput.
Android/Linux initial plus two warm sessions passed in r33 only with EATT
explicitly disabled on the BlueZ host. Restoring its default configuration
reproduced native Android status 147 with the same binaries (r34). BlueZ 5.66
attempted EATT on an unencrypted inbound connection, received rejection and
sent an unanswered SMP Security Request before the approximately 30-second
failure. This narrows the trigger but does not establish which implementation
owns the timeout bug. System-wide GATT Channels=1 was a temporary diagnostic,
not an SDK option or a shipped fix; the original configuration was restored.
The current iPhone/Linux profile, default-configuration compatibility,
multi-peer recovery and 8 KiB read-profile stress remain unresolved; see evidence. Reconnect locators and a
new C ABI link ID do not alone prove a new physical ATT session. Consumers must
reset framing and authenticate every new route; do not silently carry bytes,
identity or delivery state over a reconnect.

[BlueZ GATT API](https://github.com/bluez/bluez/blob/master/doc/org.bluez.GattCharacteristic.rst)
and [BlueZ 5.66 implementation](https://github.com/bluez/bluez/blob/5.66/src/gatt-database.c)
explain the device-scoped read and shared notification behavior. Product
capabilities must be assessed separately from this transport API.
