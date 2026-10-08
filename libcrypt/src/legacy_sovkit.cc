// Frozen source compatibility only; excluded from the C ABI SDK.
#include <libcrypt.h>
#include <sodium.h>
#include <algorithm>
#include <mutex>
bool Crypt::DeriveDatabaseKey(const XChaCha20Poly1305Key &data_key,
                             XChaCha20Poly1305Key &database_key) noexcept {
  static_assert(crypto_kdf_CONTEXTBYTES == 8);
  static_assert(crypto_kdf_KEYBYTES == 32);
  if (!Initialize()) {
    SecureZero(database_key);
    return false;
  }
  XChaCha20Poly1305Key derived{};
  const int status = crypto_kdf_derive_from_key(
      derived.data(), derived.size(), 1, "SOVDB001", data_key.data());
  if (status == 0) database_key = derived;
  else SecureZero(database_key);
  SecureZero(derived);
  return status == 0;
}

namespace {
constexpr std::array<std::uint8_t, 16> kVaultHeader = {
    'S', 'K', 'V', 'A', 'U', 'L', 'T', 1, 0, 0, 0, 3, 4, 0, 0, 0};
std::mutex vault_derivation_mutex;
struct WipedKey {
  Crypt::XChaCha20Poly1305Key value{};
  ~WipedKey() { Crypt::SecureZero(value); }
};
}

bool Crypt::CreatePasswordEnvelope(std::string_view password, Bytes &envelope,
                                   XChaCha20Poly1305Key &data_key) {
  SecureZero(data_key);
  envelope.clear();
  Bytes random;
  if (!RandomBytes(32, random)) return false;
  WipedKey generated;
  std::copy(random.begin(), random.end(), generated.value.begin());
  SecureZero(random);
  if (!RewrapPasswordEnvelope(password, generated.value, envelope)) return false;
  data_key = generated.value;
  return true;
}

bool Crypt::RewrapPasswordEnvelope(std::string_view password,
                                   const XChaCha20Poly1305Key &data_key,
                                   Bytes &envelope) {
  envelope.clear();
  if (password.empty() || password.size() > 1024) return false;
  std::unique_lock lock(vault_derivation_mutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  Bytes random;
  if (!RandomBytes(16 + 24, random)) return false;
  PasswordHashSalt salt{};
  XChaCha20Poly1305Nonce nonce{};
  WipedKey wrapping;
  std::copy_n(random.begin(), 16, salt.begin());
  std::copy_n(random.begin() + 16, 24, nonce.begin());
  SecureZero(random);
  if (!Argon2id(password, salt, 3, 64 * 1024 * 1024, wrapping.value)) return false;
  Bytes header(kVaultHeader.begin(), kVaultHeader.end());
  header.insert(header.end(), salt.begin(), salt.end());
  header.insert(header.end(), nonce.begin(), nonce.end());
  Bytes ciphertext;
  if (!XChaCha20Poly1305Encrypt(data_key, header, wrapping.value, nonce,
                                ciphertext)) return false;
  envelope = std::move(header);
  envelope.insert(envelope.end(), ciphertext.begin(), ciphertext.end());
  return true;
}

bool Crypt::OpenPasswordEnvelope(std::string_view password,
                                 std::span<const std::uint8_t> envelope,
                                 XChaCha20Poly1305Key &data_key) {
  SecureZero(data_key);
  if (password.empty() || password.size() > 1024 ||
      envelope.size() != kPasswordEnvelopeSize ||
      !std::equal(kVaultHeader.begin(), kVaultHeader.end(), envelope.begin()))
    return false;
  std::unique_lock lock(vault_derivation_mutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  PasswordHashSalt salt{};
  XChaCha20Poly1305Nonce nonce{};
  std::copy_n(envelope.begin() + 16, 16, salt.begin());
  std::copy_n(envelope.begin() + 32, 24, nonce.begin());
  WipedKey wrapping;
  if (!Argon2id(password, salt, 3, 64 * 1024 * 1024, wrapping.value)) return false;
  Bytes plaintext;
  const bool valid = XChaCha20Poly1305Decrypt(envelope.subspan(56),
      envelope.first(56), wrapping.value, nonce, plaintext) && plaintext.size() == 32;
  if (valid) std::copy(plaintext.begin(), plaintext.end(), data_key.begin());
  SecureZero(plaintext);
  return valid;
}
