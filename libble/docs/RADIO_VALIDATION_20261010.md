# libble synthetic radio validation — 2026-10-10

> Later consumer follow-up, 2026-10-10: SovKit retired browser network sharing, its proxy implementation and dedicated UI/helper. The retained ABI returns NOT_SUPPORTED; consumer Mac CTest now passes 91/91 and Flutter 442 tests pass with 1 skip. Three browser-only CTests were removed and two retirement guards added; the prior SOCKS 0x04 remains historical evidence, not a fixed defect. Mac/iOS SDK and final Mac Flutter builds pass. No new libble radio test or BlueZ change was performed; the r36/r37 and Linux blockers below remain. See SovKit docs/sovkit.zhiyu/BROWSER_RETIREMENT_20261010.md.

## Current result after legacy consumer removal and r37

The user superseded conditional retirement: remove the old consumer BLE now,
then regress and hand off blockers. SovKit deleted 23 old source/test/script
files, both message and invitation-GATT engines, Flutter channels and build
wiring. NFC and SovKit business authentication/framing/recovery/ACK remain.
Consumers now have an architecture regression rejecting native radio engines.
libble is the sole radio implementation; no old fallback remains.

Current nine-byte epoch profile, 128/512/1024-byte batches and 25-second send
budget: r36 Windows peripheral with Android/iOS/macOS/Linux centrals passed on
all five nodes. Windows passed=4/links=4/failed=0; each other node 1/1/0.
All 24 received payload hashes match ACKs in counterpart logs. This is finite
bidirectional byte traffic, not four-way continuous load or product acceptance.

r37 Linux peripheral, 90 seconds: Linux passed=3/links=6/failed=1; macOS and
Windows each passed=1/links=1/failed=0. Android passed=0/links=2/failed=1,
native status147 and send-failed -4. iOS passed=1/links=2/failed=1: the first
session timed out, the subsequent session completed. The round failed; a later
PASS cannot erase first-session errors. There was no new btmon capture or
system EATT change, so these logs do not identify a platform root cause.
r35 missed iPhone installation and launched the normal app: exclude its iOS
evidence and whole-five-node claim; only the other four-node subset is valid.
Explicit fixture installation and runtime markers were verified for r36/r37.

Prior controlled evidence remains: default BlueZ5.66 r31 had unencrypted EATT
refusal, unanswered SMP Security Request then ~30-second termination. Local
ATT requests/responses matched; HCI TX is not proof of a remote callback.
r33 verified Channels=1 and passed 3 logical sessions/two warm reconnects on
one physical connection with 18 matching payload/ACK pairs; restored-default
r34 used the same binaries and failed again. r32 configuration was invalid.
Astra found no public per-connection EATT disable. BlueZ5.67 first contains
1033a462377d9374e9240878357620a8ad368bbf (initiator gate/DEFER_SETUP) and
ab3ff0d2cd5aab8bde5a99cf76e4771eefa7f7a0 (default Channels=1). Validate a
distribution-maintained build and the role/config variables separately; no
upgrade has been performed. Host kernel6.19.8-surface-3 has not had its precise
patch set or the phone's SMP timeout path directly confirmed.

The libble truthful stop/control barrier and retired-epoch isolation remain.
SovKit additionally fixed premature off in asynchronous ClientNearby stop:
stopping until current backend confirmation, failed/error on rejection,
separate stop/start generations and no stopped candidate/link/data revival.
Actual production lifecycle tests cover failure, retry and stale completion.
No libble ABI or package was replaced for this consumer-only state fix.

Latest native libble checks: macOS2/2 CTest, Windows/Linux portable1/1 each,
Android/iOS fixtures rebuilt, actual Android bounded callback queue passed.
Linux was built in Ubuntu22.04.5 rootfs/bwrap; maximum GLIBC2.34,
GLIBCXX3.4.29,CXXABI1.3.9. It ran on Debian12/BlueZ5.66, not Docker radio.
SovKit Mac/iOS rebuilt with ABI27/97 exports; final Mac full CTest91/92 passed,
remaining browser_network SOCKS0x04. Missing SDK docs were fixed. Flutter
analyze and457 tests passed/1 skipped; complete Mac consumer and retained
actual Android NFC compiled. Complete Android consumer lacked the current
prebuilt SovKit/JNI package; no fake library bypass was used.

Valid radio rounds use synthetic BLE-TEST bytes and fixture ACKs, no product
identity, Noise, encrypted business messages or durable receipts. r35's invalid
normal iPhone launch opened its business store; no test messages were sent to
it and no current user identity/data/key was removed. Normal signed iPhone app
and Pixel UI were restored after r37. BlueZ config hash remains
0ad8f52a999a743f9ff36a532f3c59dd2cbfacdda1df8f4034ffb9ef6f9b7a2b.
All raw radio logs/captures remain private. Full non-Apple business-core,
all roles, 8KiB pressure, lockscreen/background/restart and continuous-load
acceptance remain open. Legacy removal is completed, not a pending gate.

Historical legacy validation covered Pixel/iOS/macOS/Windows only (user
clarification), no Linux or equivalent rigor. It is provenance, not a certified
five-platform baseline. Do not restore it to mask remaining compatibility bugs.

## Compatibility scope for later discussion

The user clarified that Bluetooth can behave differently across devices and
that compatibility with every device cannot be promised. libble remains the
common implementation; interoperability conclusions apply only to the tested
device, OS/Bluetooth-stack version and connection-role combinations.

The support matrix, minimum platform requirements, behavior in unsupported
environments and external compatibility commitments remain open for a separate
discussion. Device variation does not remove the current reproduced defects or
their acceptance requirements. Legacy removal was subsequently authorized
immediately and has been completed; support-scope discussion remains deferred.

## Earlier experiments through r26

These are development-preview results, not SovKit message acceptance. Tests used
five user-authorized, Bluetooth-enabled physical hosts and a separate BLE-TEST
service profile. They never opened product identity, pairing, SQLCipher or chat
storage. Nodes exchanged UTF-8 synthetic data with per-link route tags, FNV-64
integrity checks and application ACKs. FNV is not cryptographic authentication.

Normal cases: 128 / 512 / 1024 bytes, 25-second native send budget. Each tested
link needs all three inbound frames and all three matching outbound ACKs.
A disconnected/incomplete extra link makes the round incomplete even when an
earlier link passed. A zero-link RESULT is not interoperability success.
Early stress rounds used 128 / 1024 / 8192 bytes; r10 alone used a 60-second
budget. Those failures are retained and are not replaced by the smaller cases.
Round IDs below are local evidence identifiers; raw logs stay private.

## Hosts and build checks

| Host | Build / radio scope |
| --- | --- |
| Mac arm64 | macOS minimum 13, CoreBluetooth; two native CTest checks |
| iPhone SE 1st gen | iOS 15.8.8, minimum 15 framework; device execution |
| Pixel 7 Pro | Android 17, NDK arm64 API 24 baseline, SDK-owned Kotlin/JNI |
| Windows x64 | Windows 11, MSVC /MT, WinRT; portable state CTest |
| Debian x64 | Debian 12, BlueZ 5.66; Ubuntu 22.04.5 bwrap build, GIO 2.72.4; portable CTest |

Debian's existing build isolation is bwrap over Ubuntu 22.04, rather than a
Docker CLI workflow. Radio execution uses the Debian host's powered adapter
and system bus. This does not validate access to a Bluetooth radio from Docker.
The portable state test exercises actual common C ABI queue/lifetime/thread
behavior with a fake radio. Apple state tests exercise the actual Swift state
paths, including final write completion while a read is pending. No CTest
starts a radio or reads real identities. Separate native fixtures require
explicit invocation. Build success does not establish background support.

## Evidence by role

| Advertiser | Scanners | Evidence | Result |
| --- | --- | --- | --- |
| iPhone | Mac, Windows, Pixel | r3 (8 KiB), r22/r23 (normal) | Each pair completed; r23 also had an incomplete extra inbound link, so its whole round is incomplete |
| iPhone | Debian | r22/r23; isolated foreground r24 | No ready GATT link; not passed |
| Pixel | Mac, Windows, iPhone | r5 (8 KiB), r26 (normal) | Each pair completed |
| Pixel | Debian | r5, r26 | A normal route completed, but repeated timeout/reconnect routes were incomplete; whole round failed |
| Debian | Mac | r2 single-peer old notification experiment; r11/r13/r14 and r21 read profile | Normal read-profile bidirectional cases passed; r20 missed one Mac ACK, repaired idle pump and r21 passed |
| Debian | Windows | r11/r13/r14, r20/r21 read profile | Normal bidirectional cases passed |
| Debian | Pixel | isolated r18 read profile | Complete bidirectional normal round passed |
| Debian | iPhone | r7/r8/r10/r11/r13/r14/r15/r17/r20/r21 | Read/write stalls or timeout; not passed |
| Debian | All four | r20/r21 read profile | Whole round failed: iPhone timeout and incomplete reconnect routes; not a four-peer isolation acceptance |
| Windows | Mac, Pixel, Debian | r19 normal | Whole four-host round passed (three links on Windows) |
| Mac | iPhone, Pixel, Windows | r25 normal | Each pair completed |
| Mac | Debian | r25 normal | No ready GATT link; not passed |

## Failures, corrections and limits

r1 exposed BlueZ writes whose `type` option was omitted; both absent and
`request` are now valid with-response writes. r4 exposed cross-device data on
BlueZ's shared external notification socket. That implementation was removed,
and Linux now uses a device-scoped read-only TX profile. Do not deploy the old
notification experiment for multiple peers.

Read-profile clients strip an SDK internal 0/1 idle/data prefix. They serialize
ATT operations and honor send/connect budgets. Long writes failed on Windows
and Pixel; read-profile writers use conservative ATT-sized chunks (CoreBluetooth
uses its actual with/without-response maxima). An accepted final write completes
once independently of pending reads. Idle polling now resumes after a deferred
final write. Individual Linux GATT read/write failures close the affected link
and emit diagnostics rather than failing every route. Partial RX/TX service
object snapshots no longer permanently block resolution; bounded rechecks
continue within the existing connection budget.

Some early mobile rounds were invalid because an old lab binary or missing
Android start command was used; r6/r7/r8/r10 are not counted as mobile passes.
Android test startup now uses one explicit receiver command and does not depend
on an Activity singleton. The iOS fixture explicitly waits for foreground
activation before starting; this removes ambiguity, but isolated r24 still did
not establish the Debian/iPhone link. Normal apps retain their established
bundle IDs, signing teams and current data.

Remaining release blockers: Apple/BlueZ establishment and read-profile stalls;
sustained Linux concurrency; same-peer GATT reconnect epoch isolation; 8 KiB
read-profile stress; platform background/network-switch recovery; signed and
verified distribution outside the exercised Apple slice. A new native link ID
is not proof of a new physical ATT session. Consumer framing, authentication and
delivery state must reset on each route; no unauthenticated locator grants trust.

A first-phase SovKit gate additionally needs the same core SDK on every platform,
secure platform key/workspace adapters, authenticated two-way pairing, encrypted
short messages, durable ACK semantics, duplicate/replay protection, process
restart and Bluetooth off/on recovery. UI previews and raw-byte fixtures do not
satisfy that gate. No blanket "all five platforms validated" claim is warranted.
