# Algorithms and implementation inventory

Exact provider versions and static archive hashes are recorded in each package's
manifest.json and returned at runtime by dkcrypt_backend_versions().
The initial local macOS package uses libsodium 1.0.22 and OpenSSL 3.6.3.

| Capability | Implementation and exact construction | Consumer responsibilities |
| --- | --- | --- |
| Random / constant-time compare / wipe | libsodium randombytes_buf, sodium_memcmp, sodium_memzero | Secret lifetime and storage |
| Digests | libsodium SHA-256, SHA-512, unkeyed BLAKE2b-256 | Domain/use selection |
| Legacy digest | OpenSSL EVP MD5 | Existing non-security checks only; not authentication or collision-resistant integrity |
| HMAC | libsodium HMAC-SHA256, 32-byte key | Context binding |
| Subkey derivation | libsodium BLAKE2b KDF, key32/context8/id64/output32 | Unique domain/id assignment |
| Password derivation | libsodium Argon2id v1.3 (ALG_ARGON2ID13) | Parameters, salt and concurrency policy |
| Signatures | libsodium Ed25519, seed32 / public32 / secret64 / signature64 | Identity and trust binding |
| Key agreement | libsodium X25519, key32; rejects low-order invalid shared-secret inputs | Peer authentication |
| Sealed boxes | libsodium crypto_box_seal: ephemeral X25519 + XSalsa20-Poly1305; overhead48 | Recipient key/trust; does not authenticate a sender |
| AEAD | libsodium XChaCha20-Poly1305-IETF, key32 / nonce24 / tag16 | Nonce uniqueness and authenticated context |
| Noise | devkit's existing Noise_XX_25519_ChaChaPoly_SHA256 implementation, revision34; libsodium primitives, RFC8439 ChaCha20-Poly1305 with 96-bit nonce | Role, prologue, authenticated identity payload, lifecycle |
| Encoding | libsodium original padded Base64; existing Bitcoin-alphabet Base58 implementation | Encoding is not encryption |

Noise's implementation is covered by the noise-c vector already used by Sovkit
and an additional C ABI handshake/transport regression. There is no modified
Noise suite or application-specific handshake payload in the SDK.
Sovkit's SKVAULT envelope, SOVDB001 key domain and SVKNET2 stream layout are
excluded from this SDK and maintained in Sovkit's private policy adapter.

References: [libsodium](https://doc.libsodium.org/),
[KDF](https://doc.libsodium.org/key_derivation),
[Argon2](https://doc.libsodium.org/password_hashing/default_phf),
[Noise revision 34](https://noiseprotocol.org/noise.html),
[OpenSSL](https://docs.openssl.org/3.6/).

This is a technical inventory, not an export-compliance classification or security
certification. Packaging cryptography in a shared library does not remove it from
the consuming application's encryption inventory. TLS/QUIC and SQLCipher remain
outside this SDK; Sovkit may still carry their OpenSSL implementation until their
own packaging is addressed.
