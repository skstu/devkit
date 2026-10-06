# liblog

`liblog` is SovKit's internal structured logging core. It uses spdlog for
formatting and sinks while keeping spdlog types out of its public C++ header.

The logger is instance-based and supports JSON Lines output, bounded
asynchronous delivery, rotation, redaction, flush/shutdown barriers, and a
serialized callback sink. SovKit owns the process-wide instance and exposes a
small C callback API from `sovkit.h`.

Logging deliberately does not depend on a libuv loop. Logs must remain usable
before a context starts, while a loop is failing, and after all contexts stop.
Product runtime writes use liblog's private bounded worker queue. The legacy
logger retains its historical blocking queue and callback semantics.

```cpp
#include <liblog/liblog.h>

sovkit::log::Config config;
config.console_enabled = true;
config.file_enabled = false;

auto logger = sovkit::log::Logger::Create(config);
logger->Info("runtime", "started", "SovKit runtime started", {},
             SOVKIT_LOG_SOURCE_LOCATION);
logger->Flush();
```

Callbacks execute serially on the delivery worker and must return quickly.
Callback data is borrowed for the duration of the call. Re-entrant logging is
written to configured sinks but is not delivered recursively to the callback.

## Product runtime logger

`RuntimeLogger` in `liblog/runtime_logger.h` is the separate, opt-in product
pipeline. It never uses spdlog's async pool or calls a host callback. One owned
worker invokes synchronous spdlog sinks. A producer uses `try_lock`, and publishes
to the runtime ring before submitting to the file queue. Slow IO cannot hold the
producer lock. Contention and overload are observable losses, including ERROR.

Defaults are 2000 entries / 2 MiB for the ring, 1024 entries / 4 MiB for the queue,
with 128 entries / 512 KiB reserved for WARN and above. Both limits apply, and
charged bytes include record allocation/metadata estimates. The in-flight record
remains charged to the queue. Serialization is bounded to 8 KiB per record.
Flush barriers have separate control state, so they cannot be dropped by overload.

The SDK starts no product logger and touches no product log directory until
`sovkit_log_configure`. Example inputs, passed with exact UTF-8 byte lengths:

```json
{"version":1,"directory":"/private/absolute/private-log-root","role":"app","console":false,"minimumLevel":"info","fileLevel":"info"}
```

```json
{"version":1,"level":"info","component":"transfer","event":"transfer.phase_completed","code":"OK","fields":{"phase":"verify","duration_ms":128}}
```

```json
{"session":"","afterSeq":"0","minimumLevel":"info","maxCount":200,"maxBytes":262144}
```

Configuration accepts only these six keys; `version` and `directory` are
required. Directory must be an absolute UTF-8 path, at most 4096 bytes, with no
NUL, dot components, or trailing separator. Accepted roles are `app`, `helper`,
`sdk`, `sovkitd`, and `stun`. Levels are lowercase `trace`, `debug`, `info`, `warn`,
`error`, `critical`, `off`. Only levels and console can change during a live
session. To change directory/role or retry a failed file sink, first finish
shutdown and then configure a new session. DEBUG/TRACE expire after 30 minutes
using the monotonic clock. They do not weaken privacy validation.

Input JSON is limited to 16 KiB. Duplicate keys, malformed UTF-8, deep nesting,
unknown keys, wrong types, negative/overflowing bounds and noncanonical decimal
cursors are rejected. Read `maxCount` is 1–200 and `maxBytes` is 12288–262144,
including the entire response envelope and stats. The minimum byte budget
guarantees room for one maximum record plus metadata. All output buffers are
owned, non-NUL-terminated, and released with `sovkit_free`, including a nonempty
emit response accompanying an overload error. No caller pointers are retained.

Read returns `{session,records,nextCursor,gap,stats}`. Each caller owns its cursor;
reading never drains the ring. Filtered scans advance the cursor. A session
mismatch, cursor beyond the current session, or ring eviction reports `gap`.
Producer contention before sequence assignment is reported in dropped counters;
it does not manufacture a sequence hole. Session/sequence/operation/timestamps
are generated internally. Operation values are currently unique per event;
cross-event operation correlation is not exposed to hosts yet.

The template catalog and per-event field groups are in `runtime_logger.cc`.
Hosts cannot supply a raw message, stack/source location, arbitrary operation or
object ID, URL, address, path or filename. String values are finite enums (for
example `phase`, `family`, `path` category, `protocol`, `error_class`, `scope`).
Numbers/booleans have explicit ranges. Startup version/build fields are numeric
components. Event names cover SDK/runtime, identity, vault, storage diagnostics,
pairing/reconnect, discovery/transport, messaging, transfers, authorization,
Flutter exceptions, and platform lifecycle/errors. Their availability does not
mean those business services have been instrumented. Unknown native events map
to `sdk.diagnostic`; native free text/source/IDs are discarded and a `truncated`
flag records omitted fields. Native nonzero errors currently map to `ERROR`.

Product configuration suppresses the legacy logger's disk/console sinks, even
for its queued records. Registered native callbacks keep the legacy borrowed
record contract and may still receive raw native text; they are not a product
output or a Dart listener interface. Their historical callback/backpressure
semantics remain. Internal legacy reconfiguration is rejected after product mode
is enabled, and shutdown does not silently restore the default console.

## Files and lifecycle

On POSIX, the worker walks directories using `openat` and `O_NOFOLLOW`, anchors
operations to a directory descriptor, and creates files exclusively at 0600.
Apple uses `O_SEARCH` directory anchors with the same `O_NOFOLLOW` component
checks, matching libdb's path policy. This avoids requesting directory-read
permission on global ancestors such as `/` or `/private` in App Sandbox. Only
the retention scanner opens `.` for reading inside the final private log root.
Linux/Android use `O_PATH | O_DIRECTORY` where available for the same reason;
other POSIX targets fall back to read-only directory anchors. No branch
canonicalizes and then performs an unguarded open.
The log root must be owned by the current user with mode 0700; missing components
are created privately, while existing unsafe permissions are rejected rather
than changed. The host should supply a canonical platform path (for example,
`/private/var/...` instead of a symlinked `/var/...`).

Files are named `role-YYYYMMDDTHHMMSSZ-pid-<32hex-session>.<segment>.jsonl`.
Segments rotate at 10 MiB. A role-specific lock coordinates cleanup and each
active file holds its own lock. Only exact-name, private, single-link, regular,
closed files of that role are eligible for deletion. Symlinks/hardlinks or unsafe
managed files disable the file sink; exports, unrelated names, other roles and
active files are preserved. Each role has its own explicit 50 MiB budget across
sessions; this is not a 50 MiB limit on all roles combined. Age retention is seven
days by file mtime, checked at startup, before writes and during periodic flush.
Capacity remains enforced under clock rollback. Directory scans are capped at
4096 entries; an unmanageable root fails closed. Quota or space-check failure
stops file output without stopping runtime reads. Cleanup never opens a database
or touches business data, and no runtime directory is recursively deleted.

File startup happens on the worker: successful configure means the pipeline was
created, not that disk is healthy. Inspect `stats.file.state` (`starting`,
`healthy`, `unavailable`, `closed`), its fixed error code, errors, written count,
and last successful flush time. Dropped counters by level/sink, queue high-water
counts/bytes, pending work and timeout counters are independent of emitted logs.
Raw OS exception text/paths are never included in health reports. A failed file
sink stays unavailable for the session; the runtime ring remains available.

Worker flush is every two seconds and after ERROR/CRITICAL. Flush means buffered
output was passed to the OS, not `fsync` or power-loss durability. Normal shutdown
has a three-second budget (explicit SDK budgets are capped at 30 seconds).
`Shutdown` may return false because disk failed even though `IsClosed()` is true;
`safeToUnload` only becomes true after the worker closes its sinks and is joined.
If still closing, the SDK retains the owner and callers may read stats/ring and
retry shutdown. Never unload the SDK while it is closing. No worker is detached.
Abandoning a C++ owner before successful joining intentionally retains its state
for process lifetime, avoiding a use-after-free or a hidden blocking destructor.

Windows currently keeps the runtime ring but fails file startup with
`secure_file_backend_unavailable`: a handle-relative, private-ACL/reparse-safe
backend still needs implementation and Windows validation. POSIX implementation
has passed isolated native integration tests in macOS App Sandbox, iOS
Simulator and Android Emulator. Linux and Windows still need target validation.
The Apple host excludes its log root from backup; Flutter export is a previewed,
bounded current view, not a full historical-session archive. See the
[validation record](../../docs/sdk/SQLITE_LOGGING_VALIDATION_20260910.md).

## Standalone verification and SDK integration

`tests/cpp/runtime_logging_test.cpp` is an executable test without a test-framework
dependency. Compile it with all three liblog sources, C++20, pthreads,
`FMT_HEADER_ONLY`, `SPDLOG_FMT_EXTERNAL`, `SOVKIT_RUNTIME_LOG_TESTING`, and cached
spdlog/fmt/nlohmann include paths. The test-only worker hook blocks or fails IO
deterministically; it is absent from production builds.

To additionally exercise the four C ABI functions and product/native callback
isolation, compile `projects/sdk/src/logging.cpp` and the generated SDK
`error.cpp`, add the SDK source/public/generated include directories, and define
`SOVKIT_BUILD`, `SOVKIT_RUNTIME_LOG_SDK_TESTING` and
`SOVKIT_RUNTIME_LOG_STANDALONE`. The last definition provides test-only last-error
storage, not an SDK substitute. Run the same executable with
`-fsanitize=address,undefined -fno-omit-frame-pointer` for lifetime/bounds checks.
All fixtures use fresh temporary directories; tests never open product data.

The complete source/definition setup is provided in
`tests/cpp/runtime_logging.cmake`. The main test CMake only needs
`include("${CMAKE_CURRENT_LIST_DIR}/runtime_logging.cmake")` after its test helper.
Build `sovkit_runtime_logging_test` and run `ctest -R '^sovkit.runtime_logging$'`.
The include also works from a standalone CMake harness if
`SOVKIT_RUNTIME_LOG_GENERATED_DIR` points to an existing generated SDK directory.
It does not link the full SDK or storage stack. Run the existing `sovkit.logging`
and process-exit tests separately against the newly built production SDK.

The SDK integration owner must wire the four public declarations and export
whitelists/build manifest, advertise capability/version in `sovkit_info`, update
generated bindings, and register this test target. Product hosts must configure
logging before SDK initialization, poll owned JSON, expose file health, and
complete shutdown before unloading. No SDK ABI number or product-level
integration/test completion is claimed by this library change.
