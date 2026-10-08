# C ABI v1

Include `libcrypt/crypt.h`. Status and algorithm identifiers have fixed-width
integer representations. Span/buffer lengths use the platform's size_t; use a
package matching your platform and architecture. No STL, exceptions, allocator,
provider struct, or owning pointer to a caller buffer crosses the boundary.

- Initialize with ABI version 1. A different version returns ABI_MISMATCH.
- A span may contain NULL only when its size is zero.
- Caller owns all span/buffer memory. Do not overlap input and output memory.
- On success buffer.size is the number of bytes written (no implicit NUL).
- BUFFER_TOO_SMALL reports a required capacity without consuming randomness,
  advancing a Noise session/counter or finalizing a hash. For encoding/decoding,
  this capacity can be a conservative bound, not the eventual output length.
- Other output failures set size to zero and never expose unauthenticated
  plaintext. Caller still owns and must wipe any previous buffer contents.
- Secret keypair output is concatenated public key then private key; its caller
  must wipe the private bytes after use. Use secure_zero, not an ordinary memset.
- Stateless operations are thread-safe. Serialize each handle, including destroy.
  Destroy(NULL) is safe. Double destroy, stale handles and invalid addresses
  cannot be validated by a C ABI and are caller errors.
- Ordinary exceptions are translated to NO_MEMORY or INTERNAL_ERROR. Destroy
  handles before unloading the library. An internal Noise error invalidates
  the handle; create a new session.
- C++ wrappers allocate in the consuming application, may throw on allocation or
  handle creation, and otherwise return bool. They do not add an exported C++ ABI.

Limits: 64 MiB per general input (ciphertexts allow their overhead); Base58
8192 raw bytes / 11305 encoded bytes; Argon2id passwords 1..1024 bytes,
operations 1..10, memory 8 KiB..256 MiB. Limit concurrent Argon2 calls in the
consumer. Streaming SHA-256 permits multiple bounded updates.

Noise is exactly XX / 25519 / ChaChaPoly / SHA256, revision 34. Three messages
alternate initiator write/read/write and responder read/write/read. Message
overheads are 32, 96 and 64 bytes. All Noise messages are at most 65535 bytes;
transport plaintext is at most 65519 bytes. Authentication failure invalidates a
handshake; transport authentication failure leaves its receive counter unchanged.
A successful receive advances that counter, so a replay fails. Noise's first XX
payload is plaintext: application identity binding, peer trust and placement of
sensitive payloads remain the application's responsibility. The handshake hash
is available after completion. No deterministic ephemeral injection is exported.

A nonce must never repeat for an AEAD key. This SDK does not persist nonce counters,
store long-term keys, grant peer trust, choose a password policy or authenticate
an application's user. The application supplies these rules.

## Versioning and deployment

ABI major 1 uses a stable C prefix dkcrypt_ and SONAME devkit_crypt.1.
A breaking ABI change requires a new major/SONAME. The root devkit VERSION
identifies the build; pin a package manifest and its file hashes in consumers.
A working-tree package records its source hash and dirty state, not a claimed
published revision. Do not substitute a package just because the version matches.

A package includes headers, platform runtime, CMake import files, an independent
C consumer example, licenses, algorithms inventory, and manifest. Provider types
and provider shared libraries are absent. The loader still uses the platform's
system runtime (on macOS, libSystem and libc++). Static provider linkage is not
a claim that the platform runtime is bundled or that the binary is freestanding.
