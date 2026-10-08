#include <libcrypt.h>

#include <algorithm>
#include <limits>
#include <mutex>

#include <sodium.h>

namespace {

const std::uint8_t kEmptyInput = 0;

const std::uint8_t *DataOrEmpty(std::span<const std::uint8_t> input) {
  return input.empty() ? &kEmptyInput : input.data();
}

bool CanAppendTag(std::size_t size, std::size_t tag_size) {
  return size <= std::numeric_limits<std::size_t>::max() - tag_size;
}

} // namespace

struct Crypt::Sha256Stream::Impl {
  crypto_hash_sha256_state state{};
  bool active = false;
};

Crypt::Sha256Stream::Sha256Stream() : impl_(std::make_unique<Impl>()) {
  Reset();
}

Crypt::Sha256Stream::~Sha256Stream() { Reset(); }
Crypt::Sha256Stream::Sha256Stream(Sha256Stream &&) noexcept = default;
Crypt::Sha256Stream &
Crypt::Sha256Stream::operator=(Sha256Stream &&) noexcept = default;

bool Crypt::Sha256Stream::Update(std::span<const std::uint8_t> input) {
  return impl_ != nullptr && impl_->active && Initialize() &&
         crypto_hash_sha256_update(
             &impl_->state, DataOrEmpty(input),
             static_cast<unsigned long long>(input.size())) == 0;
}

bool Crypt::Sha256Stream::Finalize(Hash256 &output) {
  if (impl_ == nullptr || !impl_->active || !Initialize() ||
      crypto_hash_sha256_final(&impl_->state, output.data()) != 0)
    return false;
  impl_->active = false;
  sodium_memzero(&impl_->state, sizeof(impl_->state));
  return true;
}

void Crypt::Sha256Stream::Reset() noexcept {
  if (impl_ == nullptr)
    return;
  sodium_memzero(&impl_->state, sizeof(impl_->state));
  impl_->active = Initialize() && crypto_hash_sha256_init(&impl_->state) == 0;
}

bool Crypt::Initialize() noexcept {
  static const int init_status = sodium_init();
  return init_status >= 0;
}

bool Crypt::RandomBytes(std::size_t count, Bytes &output) {
  if (!Initialize()) {
    output.clear();
    return false;
  }
  output.resize(count);
  if (!output.empty()) {
    randombytes_buf(output.data(), output.size());
  }
  return true;
}

void Crypt::SecureZero(std::span<std::uint8_t> bytes) noexcept {
  if (!bytes.empty())
    sodium_memzero(bytes.data(), bytes.size());
}

bool Crypt::ConstantTimeEqual(std::span<const std::uint8_t> first,
                              std::span<const std::uint8_t> second) noexcept {
  return first.size() == second.size() &&
         (first.empty() ||
          (Initialize() &&
           sodium_memcmp(first.data(), second.data(), first.size()) == 0));
}

bool Crypt::Sha256(std::span<const std::uint8_t> input, Hash256 &output) {
  return Initialize() &&
         crypto_hash_sha256(output.data(), DataOrEmpty(input),
                            static_cast<unsigned long long>(input.size())) == 0;
}

bool Crypt::Sha512(std::span<const std::uint8_t> input, Hash512 &output) {
  return Initialize() &&
         crypto_hash_sha512(output.data(), DataOrEmpty(input),
                            static_cast<unsigned long long>(input.size())) == 0;
}

bool Crypt::Blake2b256(std::span<const std::uint8_t> input, Hash256 &output) {
  return Initialize() &&
         crypto_generichash(output.data(), output.size(), DataOrEmpty(input),
                            static_cast<unsigned long long>(input.size()),
                            nullptr, 0) == 0;
}

bool Crypt::HmacSha256(std::span<const std::uint8_t> input,
                       const HmacSha256Key &key, Hash256 &output) {
  return Initialize() &&
         crypto_auth_hmacsha256(output.data(), DataOrEmpty(input),
                                static_cast<unsigned long long>(input.size()),
                                key.data()) == 0;
}

bool Crypt::Base64Encode(std::span<const std::uint8_t> input,
                         std::string &output) {
  if (!Initialize())
    return false;
  output.resize(
      sodium_base64_ENCODED_LEN(input.size(), sodium_base64_VARIANT_ORIGINAL));
  sodium_bin2base64(output.data(), output.size(), DataOrEmpty(input),
                    input.size(), sodium_base64_VARIANT_ORIGINAL);
  output.resize(output.find('\0'));
  return true;
}

bool Crypt::Base64Decode(const std::string &input, Bytes &output) {
  if (!Initialize())
    return false;
  output.resize(input.size());
  size_t length = 0;
  if (sodium_base642bin(output.data(), output.size(), input.data(),
                        input.size(), nullptr, &length, nullptr,
                        sodium_base64_VARIANT_ORIGINAL) != 0) {
    output.clear();
    return false;
  }
  output.resize(length);
  return true;
}

std::string Crypt::Base58Encode(std::span<const std::uint8_t> input) {
  static constexpr char alphabet[] =
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  size_t zeros = 0;
  while (zeros < input.size() && input[zeros] == 0)
    ++zeros;
  Bytes digits((input.size() - zeros) * 138 / 100 + 1);
  size_t length = 0;
  for (size_t i = zeros; i < input.size(); ++i) {
    int carry = input[i];
    size_t j = 0;
    for (auto it = digits.rbegin();
         (carry != 0 || j < length) && it != digits.rend(); ++it, ++j) {
      carry += 256 * *it;
      *it = static_cast<std::uint8_t>(carry % 58);
      carry /= 58;
    }
    length = j;
  }
  auto it = digits.end() - static_cast<std::ptrdiff_t>(length);
  std::string out(zeros, '1');
  while (it != digits.end())
    out += alphabet[*it++];
  return out;
}

bool Crypt::Base58Decode(const std::string &input, Bytes &output) {
  static constexpr char alphabet[] =
      "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  output.clear();
  size_t zeros = 0;
  while (zeros < input.size() && input[zeros] == '1')
    ++zeros;
  Bytes bytes((input.size() - zeros) * 733 / 1000 + 1);
  size_t length = 0;
  for (size_t i = zeros; i < input.size(); ++i) {
    const char *p = std::char_traits<char>::find(alphabet, 58, input[i]);
    if (!p)
      return false;
    int carry = static_cast<int>(p - alphabet);
    size_t j = 0;
    for (auto it = bytes.rbegin();
         (carry != 0 || j < length) && it != bytes.rend(); ++it, ++j) {
      carry += 58 * *it;
      *it = static_cast<std::uint8_t>(carry % 256);
      carry /= 256;
    }
    length = j;
  }
  auto it = bytes.end() - static_cast<std::ptrdiff_t>(length);
  output.assign(zeros, 0);
  output.insert(output.end(), it, bytes.end());
  return true;
}

bool Crypt::GenerateEd25519KeyPair(Ed25519KeyPair &output) {
  return Initialize() && crypto_sign_keypair(output.public_key.data(),
                                             output.secret_key.data()) == 0;
}

bool Crypt::Ed25519KeyPairFromSeed(const Ed25519Seed &seed,
                                   Ed25519KeyPair &output) {
  return Initialize() &&
         crypto_sign_seed_keypair(output.public_key.data(),
                                  output.secret_key.data(), seed.data()) == 0;
}

bool Crypt::Ed25519Sign(std::span<const std::uint8_t> message,
                        const Ed25519SecretKey &secret_key,
                        Ed25519Signature &signature) {
  if (!Initialize()) {
    return false;
  }
  unsigned long long signature_size = 0;
  return crypto_sign_detached(signature.data(), &signature_size,
                              DataOrEmpty(message),
                              static_cast<unsigned long long>(message.size()),
                              secret_key.data()) == 0 &&
         signature_size == signature.size();
}

bool Crypt::Ed25519Verify(std::span<const std::uint8_t> message,
                          const Ed25519Signature &signature,
                          const Ed25519PublicKey &public_key) {
  return Initialize() && crypto_sign_verify_detached(
                             signature.data(), DataOrEmpty(message),
                             static_cast<unsigned long long>(message.size()),
                             public_key.data()) == 0;
}

bool Crypt::GenerateX25519KeyPair(X25519KeyPair &output) {
  return Initialize() && crypto_box_keypair(output.public_key.data(),
                                            output.secret_key.data()) == 0;
}

bool Crypt::X25519KeyPairFromSecret(const X25519SecretKey &secret_key,
                                    X25519KeyPair &output) {
  if (!Initialize() || crypto_scalarmult_curve25519_base(
                           output.public_key.data(), secret_key.data()) != 0)
    return false;
  output.secret_key = secret_key;
  return true;
}

bool Crypt::X25519DeriveSharedSecret(const X25519SecretKey &secret_key,
                                     const X25519PublicKey &public_key,
                                     X25519SharedSecret &shared_secret) {
  return Initialize() &&
         crypto_scalarmult_curve25519(shared_secret.data(), secret_key.data(),
                                      public_key.data()) == 0;
}

bool Crypt::Seal(std::span<const std::uint8_t> plaintext,
                 const X25519PublicKey &recipient_public_key,
                 Bytes &ciphertext) {
  if (!Initialize() || !CanAppendTag(plaintext.size(), crypto_box_SEALBYTES)) {
    ciphertext.clear();
    return false;
  }
  ciphertext.resize(plaintext.size() + crypto_box_SEALBYTES);
  if (crypto_box_seal(ciphertext.data(), DataOrEmpty(plaintext),
                      static_cast<unsigned long long>(plaintext.size()),
                      recipient_public_key.data()) != 0) {
    ciphertext.clear();
    return false;
  }
  return true;
}

bool Crypt::OpenSealed(std::span<const std::uint8_t> ciphertext,
                       const X25519KeyPair &recipient_key_pair,
                       Bytes &plaintext) {
  if (!Initialize() || ciphertext.size() < crypto_box_SEALBYTES) {
    plaintext.clear();
    return false;
  }
  plaintext.resize(ciphertext.size() - crypto_box_SEALBYTES);
  if (crypto_box_seal_open(plaintext.data(), DataOrEmpty(ciphertext),
                           static_cast<unsigned long long>(ciphertext.size()),
                           recipient_key_pair.public_key.data(),
                           recipient_key_pair.secret_key.data()) != 0) {
    plaintext.clear();
    return false;
  }
  return true;
}

bool Crypt::XChaCha20Poly1305Encrypt(
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> additional_data,
    const XChaCha20Poly1305Key &key, const XChaCha20Poly1305Nonce &nonce,
    Bytes &ciphertext) {
  if (!Initialize() ||
      !CanAppendTag(plaintext.size(),
                    crypto_aead_xchacha20poly1305_ietf_ABYTES)) {
    ciphertext.clear();
    return false;
  }
  ciphertext.resize(plaintext.size() +
                    crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertext_size = 0;
  if (crypto_aead_xchacha20poly1305_ietf_encrypt(
          ciphertext.data(), &ciphertext_size, DataOrEmpty(plaintext),
          static_cast<unsigned long long>(plaintext.size()),
          DataOrEmpty(additional_data),
          static_cast<unsigned long long>(additional_data.size()), nullptr,
          nonce.data(), key.data()) != 0) {
    ciphertext.clear();
    return false;
  }
  ciphertext.resize(static_cast<std::size_t>(ciphertext_size));
  return true;
}

bool Crypt::XChaCha20Poly1305Decrypt(
    std::span<const std::uint8_t> ciphertext,
    std::span<const std::uint8_t> additional_data,
    const XChaCha20Poly1305Key &key, const XChaCha20Poly1305Nonce &nonce,
    Bytes &plaintext) {
  if (!Initialize() ||
      ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
    plaintext.clear();
    return false;
  }
  plaintext.resize(ciphertext.size() -
                   crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long plaintext_size = 0;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
          plaintext.data(), &plaintext_size, nullptr, DataOrEmpty(ciphertext),
          static_cast<unsigned long long>(ciphertext.size()),
          DataOrEmpty(additional_data),
          static_cast<unsigned long long>(additional_data.size()), nonce.data(),
          key.data()) != 0) {
    plaintext.clear();
    return false;
  }
  plaintext.resize(static_cast<std::size_t>(plaintext_size));
  return true;
}

bool Crypt::Argon2id(std::string_view password, const PasswordHashSalt &salt,
                     std::uint64_t operations_limit, std::size_t memory_limit,
                     XChaCha20Poly1305Key &output) {
  if (!Initialize() || password.empty() ||
      operations_limit < crypto_pwhash_OPSLIMIT_MIN ||
      operations_limit > crypto_pwhash_OPSLIMIT_MAX ||
      memory_limit < crypto_pwhash_MEMLIMIT_MIN ||
      memory_limit > crypto_pwhash_MEMLIMIT_MAX) {
    SecureZero(output);
    return false;
  }
  if (crypto_pwhash(output.data(), output.size(), password.data(),
                    static_cast<unsigned long long>(password.size()),
                    salt.data(), operations_limit, memory_limit,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    SecureZero(output);
    return false;
  }
  return true;
}

bool Crypt::RandomFill(std::span<std::uint8_t> output) noexcept {
    if (!Initialize())
        return false;
    if (!output.empty())
        randombytes_buf(output.data(), output.size());
    return true;
}
bool Crypt::Ed25519PublicFromSeed(const Ed25519Seed& seed, Ed25519PublicKey& output) {
    if (!Initialize())
        return false;
    SecretKey secret;
    return crypto_sign_seed_keypair(output.data(), secret.bytes.data(), seed.data()) == 0;
}
bool Crypt::Ed25519SignFromSeed(const Ed25519Seed& seed, std::span<const std::uint8_t> message, Ed25519Signature& output) {
    if (!Initialize())
        return false;
    Ed25519PublicKey public_key{};
    SecretKey secret;
    return crypto_sign_seed_keypair(public_key.data(), secret.bytes.data(), seed.data()) == 0 &&
           Ed25519Sign(message, secret.bytes, output);
}
