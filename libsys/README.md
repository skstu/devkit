# libsys

libsys is vendored from OrbitBridge and provides system, process, network,
egress, GPU, and diagnostic information to the SovKit native core.

## Platform backends

- Windows, Linux, and standalone macOS CMake builds use the full backend.
- Flutter Native Assets uses the portable backend on macOS, Android, and iOS.
- Android and iOS expose only information available to the current app. They
  do not enumerate other apps, launch processes, activate windows, or perform
  an active system egress probe.
- Web does not build this native library.

## Modules and includes

| Target | Public header | Responsibility / dependencies |
| --- | --- | --- |
| `sovrankit::libsys_console` | `libsys/console_input.h` | Bounded stdin polling; C++20, threads and OS APIs only |
| `sovrankit::libsys_directories` | `libsys/system.h` | Read-only system data directory lookup; Foundation on Apple, shell32/ole32 on Windows |
| `sovrankit::libsys_file_lock` | `libsys/file_lock.h` | Nonblocking process exclusion on a persistent lock file; OS APIs only |
| `sovrankit::libsys_file_io` | `libsys/file_io.h`, `libsys/resources.h` | File IO and resource observation; requires libuv |
| `sovrankit::libsys` | `libsys/system.h` | Existing `ISystem` facade, process/network/GPU/probes; links the small components, nlohmann JSON and platform libraries |

`libsys/platform.h` contains platform detection only. `libsys/system.h` preserves
the existing API and native process-handle representation without including
Win32 networking, GPU or shell headers. `libsys.hpp` remains a compatibility
umbrella, including the old transitive OS headers and the new console interface.
New code should use the focused headers and explicitly include any OS APIs it
uses. Public path types still map to `std::filesystem::path`.

Common logic lives in `src/`; platform implementations live in `src/windows`,
`src/posix`, `src/apple`, `src/linux` and `src/mobile`. Console line assembly,
limits and ownership live in `src/console_input.cc`; OS handles and reads stay
in its Windows/POSIX backends behind a private interface. No platform header
or platform branch is needed in the public console API.

The source manifest in `projects/sdk/build.json` also includes these backends
for non-CMake consumers. CMake compiles the small modules once and links them
into the facade, rather than compiling duplicate implementations.

To use only console input in a CMake host:

```cmake
set(LIBSYS_COMPONENTS_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/libsys)
target_link_libraries(my_app PRIVATE sovrankit::libsys_console)
```

The components-only build does not find nlohmann JSON or build system probes.
Without that option, the existing SovKit build switches retain their behavior.
Installation includes the nested public headers and the component archives.

For the file IO component, set `LIBSYS_ENABLE_FILE_IO=ON` and provide a libuv
CMake package. This option also works with `LIBSYS_COMPONENTS_ONLY=ON` and is
enabled by default in the SDK build. The full facade loads its sibling
`libstl_core` target when the host has not already added it; it does not require
the SDK root CMake project or libpath/Iconv.

## Process exclusion by workspace

The SDK workspace lease owns `<workspace>/.sovkit-workspace.lock` before it
initializes the vault, database or runtime. Another process opening that same
workspace receives `workspace_busy`; another workspace has an independent lock.
The lock is based on the actual file, not a PID, workspace ID string, executable
name, hardware fingerprint or hash of a path. Existing lock files are normal:
their presence does not mean a process is running.

The implementation is shared with `libsys::TryLockFileHandle`. The SDK keeps its
existing validated handle and directory/ACL/ownership checks; libsys does not
reopen the path after those checks. The handle stays alive until the complete
workspace lifecycle ends. SDK hosts call `sovkit_workspace_open` first and
`sovkit_workspace_close` after draining runtime, storage, callbacks and logging;
they must not separately acquire the same lock before calling workspace_open.
The legacy SDK initialization entry points that do not open a workspace are
not a substitute for this lifecycle contract.

Standalone users can use the move-only RAII wrapper:

```cpp
#include <libsys/file_lock.h>

std::error_code error;
auto lease = libsys::FileLock::TryAcquire(workspace / ".app-instance.lock", error);
if (!lease) {
    if (error == std::errc::device_or_resource_busy) {
        // Another owner is using this directory. Do not initialize its data.
    } else {
        // Report the path/access/OS error; do not pretend it is a busy workspace.
    }
    return;
}
RunApplication(); // Keep lease alive through all background-worker shutdown.
```

Link `sovrankit::libsys_file_lock`, or the full `sovrankit::libsys` target. Every
participant must agree on the same lock filename. The parent directory must
already exist and remain under the host's control. The standalone wrapper
checks the final file type/link count (and POSIX owner/mode); it does not provide
the SDK's parent-path and Windows ACL validation. The helper's absolute path
must point to local storage; remote filesystem lock semantics are not covered.

- Windows uses `LockFileEx`; handles are non-inheritable and deny file deletion
  while open. Unicode and long local drive paths are supported.
- Linux, macOS, Android and iOS use `flock(LOCK_EX | LOCK_NB)` with `O_CLOEXEC`.
  Android/iOS callers supply the real application-container path. No HOME/cwd
  inference or extra permission dialog is needed for an accessible private path.
  App extensions/processes only coordinate if they can access the same container.
- Acquisition never waits for another process. Destruction/`Reset()` closes the
  handle. Process termination releases the OS lock, provided no inherited POSIX
  descriptor remains open in a forked child. `exec` does not inherit it.
- The file is never truncated, unlinked or replaced by the helper. In particular,
  never delete a "stale" lock file: replacing it can split ownership across two
  files. On POSIX this is a cooperative lock; it cannot prohibit unrelated code
  with write permission from ignoring it or unlinking files.

`ISystem::acquire_instance_lock(name, ...)` / `release_instance_lock` remain
legacy compatibility APIs. They use a named Windows mutex or an inferred
per-user directory on POSIX; they do not implement the workspace-path contract.
The Android legacy call lacks `Context.filesDir` and returns failure. Mobile
and new path-based consumers should use `FileLock` with an explicit path;
SDK consumers should use the workspace lifecycle above.

Validation on 2026-09-25: Windows Debug tests cover separate processes, six-way
first-open contention, directory isolation, path casing/slashes, move ownership,
normal/crash release, stable file content, and invalid paths/hard links. The SDK
workspace test also verifies contention in both directions between the wrapper
and SDK. Android NDK 28.2 compilation passes for arm64 and x86_64 with warnings
treated as errors. Android runtime and Apple builds/devices were not validated
on this Windows host.

## Nonblocking console input

```cpp
#include <libsys/console_input.h>

libsys::ConsoleInput input;
while (!input.ended()) {
  if (auto line = input.Poll()) {
    if (*line == "q" || *line == "exit") break; // Host policy, not library policy.
    HandleCommand(*line);
  }
  PollApplicationEvents();
  // Let the host scheduler yield/sleep; Poll itself does not sleep.
}
```

- `Poll()` returns one complete line, including an empty line. `nullopt` means
  no complete line yet; check `ended()` for EOF. A final unterminated line is
  returned once. LF, CRLF and CR are supported, including split pipe writes.
- `ConsoleInputOptions` bounds line bytes (default 4096, maximum 1 MiB) and
  work per poll (default 256 read units). OS stream reads are buffered in 256
  bytes. Oversized input raises `length_error`; OS errors retain the native
  code in `system_error`. An error makes the reader unusable until recreated.
- Construct and poll on the same thread. One instance owns stdin reading;
  mixing it with `cin`, `getline` or other readers is unsupported. The object
  closes its duplicated handle, never the host's original stdin. Read-ahead
  bytes belong to this reader and are not put back on destruction.
- Windows uses console input events, converts UTF-16 to UTF-8, handles key
  repeats and removes an entire UTF-8 code point on backspace. Function keys
  do not trigger a second blocking read. Ctrl+Z ends interactive input; normal
  OS signal/Ctrl+C policy remains with the host. Echo can be disabled and is
  never written to a redirected stdout. This is basic command input, not a
  full terminal editor with grapheme-aware cursor movement.
- POSIX uses a duplicated descriptor with `O_NONBLOCK`, handles EINTR/EAGAIN,
  and restores a previously blocking descriptor on destruction. Descriptor
  flags are shared with stdin: the host must not concurrently change them.
  Terminal editing/echo/signals remain under the host's existing terminal mode.
  Pipes/files preserve their bytes; callers should supply UTF-8 when needed.
- Mobile builds do not create a console. A host without valid stdin gets an
  OS error; GUI event loops should use their own input mechanism.

The SDK example now consumes this component instead of maintaining a local
console header. Tests exercise real pipes and, on Windows, an isolated hidden
console with Unicode keyboard events. The Windows process/module path helpers
also use growable UTF-16 buffers instead of truncating at `MAX_PATH`.

Validated on Windows x64 with VS 2026: standalone component build/install,
console input, existing system APIs, Unicode paths longer than 300 characters,
SDK workspace and consumer lifecycle checks. The console-only executable imports
only KERNEL32.dll. The POSIX backend is implemented but has not been built or run
on Linux/macOS/iOS/Android in this Windows session.

## Upstream

- Repository: `git@github.com:memade/OrbitBridge.git`
- Path: `projects/libsys`
- Revision: `5384538e159d41c9234e93905849899e216ed7f0`

See `LICENSE` for the upstream license.
## Persistent data directories

`ISystem::GetUserAppDataDir()` resolves the host's persistent data base without
appending a product ID or creating directories:

| Platform | Source |
| --- | --- |
| Windows | `SHGetKnownFolderPath(FOLDERID_LocalAppData)` |
| macOS / iOS | Foundation `Application Support` for the actual process sandbox |
| Linux | Absolute `XDG_DATA_HOME`, otherwise absolute `HOME/.local/share` |
| Android | `GetUserAppDataDir(contextFilesDir)`; the host passes `Context.filesDir` |

An unavailable base returns an empty path. There is no cwd, passwd, or temporary
directory fallback. The Android overload rejects a supplied override on desktop
and Apple platforms; Android without a host directory fails explicitly. Existing
system aliases are resolved on Apple and Android. A returned path is not a
workspace lease or permission guarantee; SDK workspace code owns directory
creation, ownership checks, locking and access failure handling.

The SDK's `WorkspaceLease::Open` prepares missing ancestors of that base (or
an explicit host-selected final workspace path) one component at a time. It
does not require a user to pre-create Application Support or `.local/share`.
Existing ancestor permissions are preserved and links/reparse points are
rejected during traversal. Directory resolution in libsys remains read-only;
creation and the workspace lease stay in the same SDK lifecycle owner.

The SDK links `sovrankit::libsys_directories`, a small target in this library that
requires no GPU/process/network probing or nlohmann JSON. The optional full
`sovrankit::libsys` target also links it. This keeps the same directory contract
in both builds. Android host plumbing and application migration must complete
before the product switches its existing runtime roots.
