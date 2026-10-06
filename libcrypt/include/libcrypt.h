#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A small, binary-safe wrapper around the libsodium primitives most commonly
// needed by Web3 clients and wallet-adjacent services.  All keys and outputs
// are raw bytes; callers are responsible for secure storage of secret keys.
class Crypt final {
public:
  using Bytes = std::vector<std::uint8_t>;
  using Hash256 = std::array<std::uint8_t, 32>;
  using Hash512 = std::array<std::uint8_t, 64>;
  using Hash128 = std::array<std::uint8_t, 16>;
  using HmacSha256Key = std::array<std::uint8_t, 32>;
  using Ed25519Seed = std::array<std::uint8_t, 32>;
  using Ed25519PublicKey = std::array<std::uint8_t, 32>;
  using Ed25519SecretKey = std::array<std::uint8_t, 64>;
  using Ed25519Signature = std::array<std::uint8_t, 64>;
  using X25519PublicKey = std::array<std::uint8_t, 32>;
  using X25519SecretKey = std::array<std::uint8_t, 32>;
  using X25519SharedSecret = std::array<std::uint8_t, 32>;
  using XChaCha20Poly1305Key = std::array<std::uint8_t, 32>;
  using XChaCha20Poly1305Nonce = std::array<std::uint8_t, 24>;
  static constexpr std::size_t kXChaCha20Poly1305TagSize = 16;
  using PasswordHashSalt = std::array<std::uint8_t, 16>;

  struct Ed25519KeyPair {
    Ed25519PublicKey public_key{};
    Ed25519SecretKey secret_key{};
  };

  struct X25519KeyPair {
    X25519PublicKey public_key{};
    X25519SecretKey secret_key{};
  };

  // Explicit initialization is optional: every operation initializes
  // libsodium on first use and returns false if initialization fails.
  [[nodiscard]] static bool Initialize() noexcept;
  // Domain-separated business-database key; never use the user's password as
  // an SQLCipher key. The context/id are part of the v1 storage format.
  [[nodiscard]] static bool DeriveDatabaseKey(
      const XChaCha20Poly1305Key &data_key,
      XChaCha20Poly1305Key &database_key) noexcept;
  [[nodiscard]] static bool RandomBytes(std::size_t count, Bytes &output);
  [[nodiscard]] static bool RandomFill(std::span<std::uint8_t> output) noexcept;
  [[nodiscard]] static bool Ed25519PublicFromSeed(const Ed25519Seed&, Ed25519PublicKey&);
  [[nodiscard]] static bool Ed25519SignFromSeed(const Ed25519Seed&, std::span<const std::uint8_t>, Ed25519Signature&);
  // Wipes seed-derived temporary private keys even when an operation throws.
  class SecretKey final {
  public:
      Ed25519SecretKey bytes{};
      SecretKey() = default;
      SecretKey(const SecretKey&) = delete;
      SecretKey& operator=(const SecretKey&) = delete;
      ~SecretKey() {
          SecureZero(bytes);
      }
  };
  static void SecureZero(std::span<std::uint8_t> bytes) noexcept;
  [[nodiscard]] static bool
  ConstantTimeEqual(std::span<const std::uint8_t> first,
                    std::span<const std::uint8_t> second) noexcept;

  [[nodiscard]] static bool Sha256(std::span<const std::uint8_t> input,
                                   Hash256 &output);
  // Incremental SHA-256 for bounded-memory file and stream verification.
  // The implementation owns and cleanses its provider state; callers never
  // depend on libsodium/OpenSSL structures.
  class Sha256Stream final {
  public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(Sha256Stream &&) noexcept;
    Sha256Stream &operator=(Sha256Stream &&) noexcept;
    Sha256Stream(const Sha256Stream &) = delete;
    Sha256Stream &operator=(const Sha256Stream &) = delete;

    [[nodiscard]] bool Update(std::span<const std::uint8_t> input);
    [[nodiscard]] bool Finalize(Hash256 &output);
    void Reset() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
  [[nodiscard]] static bool Sha512(std::span<const std::uint8_t> input,
                                   Hash512 &output);
  // BLAKE2b-256, used by Solana and several Substrate-based protocols.
  [[nodiscard]] static bool Blake2b256(std::span<const std::uint8_t> input,
                                       Hash256 &output);
  [[nodiscard]] static bool Md5(std::span<const std::uint8_t> input,
                                Hash128 &output);
  [[nodiscard]] static bool HmacSha256(std::span<const std::uint8_t> input,
                                       const HmacSha256Key &key,
                                       Hash256 &output);
  [[nodiscard]] static bool Base64Encode(std::span<const std::uint8_t> input,
                                         std::string &output);
  [[nodiscard]] static bool Base64Decode(const std::string &input,
                                         Bytes &output);
  [[nodiscard]] static std::string
  Base58Encode(std::span<const std::uint8_t> input);
  [[nodiscard]] static bool Base58Decode(const std::string &input,
                                         Bytes &output);

  [[nodiscard]] static bool GenerateEd25519KeyPair(Ed25519KeyPair &output);
  [[nodiscard]] static bool Ed25519KeyPairFromSeed(const Ed25519Seed &seed,
                                                   Ed25519KeyPair &output);
  [[nodiscard]] static bool Ed25519Sign(std::span<const std::uint8_t> message,
                                        const Ed25519SecretKey &secret_key,
                                        Ed25519Signature &signature);
  [[nodiscard]] static bool Ed25519Verify(std::span<const std::uint8_t> message,
                                          const Ed25519Signature &signature,
                                          const Ed25519PublicKey &public_key);

  [[nodiscard]] static bool GenerateX25519KeyPair(X25519KeyPair &output);
  [[nodiscard]] static bool
  X25519KeyPairFromSecret(const X25519SecretKey &secret_key,
                          X25519KeyPair &output);
  [[nodiscard]] static bool
  X25519DeriveSharedSecret(const X25519SecretKey &secret_key,
                           const X25519PublicKey &public_key,
                           X25519SharedSecret &shared_secret);

  // Authenticated public-key encryption. The output includes the ephemeral
  // public key and authentication tag required by OpenSealed().
  [[nodiscard]] static bool Seal(std::span<const std::uint8_t> plaintext,
                                 const X25519PublicKey &recipient_public_key,
                                 Bytes &ciphertext);
  [[nodiscard]] static bool OpenSealed(std::span<const std::uint8_t> ciphertext,
                                       const X25519KeyPair &recipient_key_pair,
                                       Bytes &plaintext);

  // XChaCha20-Poly1305 AEAD. Ciphertext includes the authentication tag;
  // callers must provide the same AAD and nonce when decrypting.
  [[nodiscard]] static bool
  XChaCha20Poly1305Encrypt(std::span<const std::uint8_t> plaintext,
                           std::span<const std::uint8_t> additional_data,
                           const XChaCha20Poly1305Key &key,
                           const XChaCha20Poly1305Nonce &nonce,
                           Bytes &ciphertext);
  [[nodiscard]] static bool
  XChaCha20Poly1305Decrypt(std::span<const std::uint8_t> ciphertext,
                           std::span<const std::uint8_t> additional_data,
                           const XChaCha20Poly1305Key &key,
                           const XChaCha20Poly1305Nonce &nonce,
                           Bytes &plaintext);

  // Argon2id password derivation. Limits are serialized by higher-level
  // formats so readers can enforce their own resource ceilings before use.
  [[nodiscard]] static bool Argon2id(std::string_view password,
                                     const PasswordHashSalt &salt,
                                     std::uint64_t operations_limit,
                                     std::size_t memory_limit,
                                     XChaCha20Poly1305Key &output);

  // Independent ordered stream cipher. Use a fresh grant key, distinct keys
  // per direction, and a unique stream number under that key. Context binds
  // the authenticated relationship/session/grant. Counters are per stream,
  // so interleaving other streams never changes this stream's nonce state.
  class StreamCipher final {
  public:
    StreamCipher(const XChaCha20Poly1305Key &key, Bytes context,
                 std::uint64_t stream);
    ~StreamCipher();
    StreamCipher(const StreamCipher &) = delete;
    StreamCipher &operator=(const StreamCipher &) = delete;
    [[nodiscard]] bool Encrypt(std::span<const std::uint8_t> plaintext,
                               Bytes &record);
    [[nodiscard]] bool Decrypt(std::span<const std::uint8_t> record,
                               Bytes &plaintext);

  private:
    XChaCha20Poly1305Key key_;
    Bytes context_;
    std::uint64_t stream_, send_ = 0, receive_ = 0;
  };

    static constexpr std::size_t kPasswordEnvelopeSize = 104;
    [[nodiscard]] static bool CreatePasswordEnvelope(
      std::string_view password, Bytes &envelope, XChaCha20Poly1305Key &data_key);
    [[nodiscard]] static bool OpenPasswordEnvelope(
      std::string_view password, std::span<const std::uint8_t> envelope,
      XChaCha20Poly1305Key &data_key);
    // Rewrap the existing random data key; records and device identity do not change.
    [[nodiscard]] static bool RewrapPasswordEnvelope(
      std::string_view password, const XChaCha20Poly1305Key &data_key, Bytes &envelope);

    // Noise Protocol Framework revision 34, exact suite
  // Noise_XX_25519_ChaChaPoly_SHA256. This stateful wrapper deliberately
  // exposes no individual chaining keys or nonces.
  class NoiseXX final {
  public:
    enum class Role : std::uint8_t { initiator, responder };

    NoiseXX(Role role, const X25519KeyPair &static_key,
            std::span<const std::uint8_t> prologue = {});
    ~NoiseXX();
    NoiseXX(NoiseXX &&) noexcept;
    NoiseXX &operator=(NoiseXX &&) noexcept;
    NoiseXX(const NoiseXX &) = delete;
    NoiseXX &operator=(const NoiseXX &) = delete;

#ifdef LIBCRYPT_TESTING
    // Test-only determinism hook. Production callers omit this and receive a
    // fresh random ephemeral key. Must be called before the first message.
    [[nodiscard]] bool
    SetEphemeralForTesting(const X25519SecretKey &secret_key);
#endif
    [[nodiscard]] bool WriteMessage(std::span<const std::uint8_t> payload,
                                    Bytes &message);
    [[nodiscard]] bool ReadMessage(std::span<const std::uint8_t> message,
                                   Bytes &payload);
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] bool GetHandshakeHash(Hash256 &output) const;
    [[nodiscard]] bool EncryptTransport(std::span<const std::uint8_t> plaintext,
                                        Bytes &ciphertext);
    [[nodiscard]] bool
    DecryptTransport(std::span<const std::uint8_t> ciphertext,
                     Bytes &plaintext);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
};
