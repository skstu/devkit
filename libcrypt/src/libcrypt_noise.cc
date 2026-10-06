#include <libcrypt.h>

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace {

using Bytes = Crypt::Bytes;
using Hash = Crypt::Hash256;
using KeyPair = Crypt::X25519KeyPair;

constexpr std::string_view kProtocolName = "Noise_XX_25519_ChaChaPoly_SHA256";
constexpr std::size_t kDhSize = 32;
constexpr std::size_t kTagSize = 16;

void Append(Bytes &output, std::span<const std::uint8_t> value) {
  output.insert(output.end(), value.begin(), value.end());
}

bool HashParts(std::span<const std::uint8_t> first,
               std::span<const std::uint8_t> second, Hash &output) {
  Bytes input;
  input.reserve(first.size() + second.size());
  Append(input, first);
  Append(input, second);
  const bool ok = Crypt::Sha256(input, output);
  Crypt::SecureZero(input);
  return ok;
}

bool Hmac(const Hash &key, std::span<const std::uint8_t> data, Hash &output) {
  return Crypt::HmacSha256(data, key, output);
}

bool Hkdf2(const Hash &chaining_key,
           std::span<const std::uint8_t> input_key_material, Hash &output1,
           Hash &output2) {
  Hash temporary{};
  const std::array<std::uint8_t, 1> one{1};
  Bytes second_input;
  bool ok = Hmac(chaining_key, input_key_material, temporary) &&
            Hmac(temporary, one, output1);
  if (ok) {
    second_input.assign(output1.begin(), output1.end());
    second_input.push_back(2);
    ok = Hmac(temporary, second_input, output2);
  }
  Crypt::SecureZero(temporary);
  Crypt::SecureZero(second_input);
  return ok;
}

class CipherState final {
public:
  ~CipherState() { Clear(); }

  void Clear() noexcept {
    Crypt::SecureZero(key_);
    has_key_ = false;
    nonce_ = 0;
  }

  void InitializeKey(const Hash &key) {
    key_ = key;
    nonce_ = 0;
    has_key_ = true;
  }

  bool Encrypt(std::span<const std::uint8_t> ad,
               std::span<const std::uint8_t> plaintext, Bytes &ciphertext) {
    if (!has_key_) {
      ciphertext.assign(plaintext.begin(), plaintext.end());
      return true;
    }
    if (nonce_ == std::numeric_limits<std::uint64_t>::max())
      return false;
    std::array<std::uint8_t, crypto_aead_chacha20poly1305_ietf_NPUBBYTES>
        nonce{};
    for (std::size_t index = 0; index < 8; ++index)
      nonce[4 + index] = static_cast<std::uint8_t>(nonce_ >> (index * 8));
    ciphertext.resize(plaintext.size() + kTagSize);
    unsigned long long size = 0;
    const int status = crypto_aead_chacha20poly1305_ietf_encrypt(
        ciphertext.data(), &size, plaintext.data(), plaintext.size(), ad.data(),
        ad.size(), nullptr, nonce.data(), key_.data());
    if (status != 0) {
      ciphertext.clear();
      return false;
    }
    ciphertext.resize(static_cast<std::size_t>(size));
    ++nonce_;
    return true;
  }

  bool Decrypt(std::span<const std::uint8_t> ad,
               std::span<const std::uint8_t> ciphertext, Bytes &plaintext) {
    if (!has_key_) {
      plaintext.assign(ciphertext.begin(), ciphertext.end());
      return true;
    }
    if (nonce_ == std::numeric_limits<std::uint64_t>::max() ||
        ciphertext.size() < kTagSize)
      return false;
    std::array<std::uint8_t, crypto_aead_chacha20poly1305_ietf_NPUBBYTES>
        nonce{};
    for (std::size_t index = 0; index < 8; ++index)
      nonce[4 + index] = static_cast<std::uint8_t>(nonce_ >> (index * 8));
    plaintext.resize(ciphertext.size() - kTagSize);
    unsigned long long size = 0;
    const int status = crypto_aead_chacha20poly1305_ietf_decrypt(
        plaintext.data(), &size, nullptr, ciphertext.data(), ciphertext.size(),
        ad.data(), ad.size(), nonce.data(), key_.data());
    if (status != 0) {
      Crypt::SecureZero(plaintext);
      plaintext.clear();
      return false;
    }
    plaintext.resize(static_cast<std::size_t>(size));
    ++nonce_;
    return true;
  }

private:
  Hash key_{};
  std::uint64_t nonce_ = 0;
  bool has_key_ = false;
};

class SymmetricState final {
public:
  bool Initialize(std::span<const std::uint8_t> prologue) {
    if (kProtocolName.size() <= h_.size()) {
      std::copy(kProtocolName.begin(), kProtocolName.end(), h_.begin());
    } else {
      const auto protocol = std::span(
          reinterpret_cast<const std::uint8_t *>(kProtocolName.data()),
          kProtocolName.size());
      if (!Crypt::Sha256(protocol, h_))
        return false;
    }
    ck_ = h_;
    return MixHash(prologue);
  }

  bool MixHash(std::span<const std::uint8_t> data) {
    Hash next{};
    if (!HashParts(h_, data, next))
      return false;
    h_ = next;
    return true;
  }

  bool MixKey(std::span<const std::uint8_t> input) {
    Hash next_ck{};
    Hash temporary_key{};
    if (!Hkdf2(ck_, input, next_ck, temporary_key))
      return false;
    ck_ = next_ck;
    cipher_.InitializeKey(temporary_key);
    Crypt::SecureZero(temporary_key);
    return true;
  }

  bool EncryptAndHash(std::span<const std::uint8_t> plaintext,
                      Bytes &ciphertext) {
    return cipher_.Encrypt(h_, plaintext, ciphertext) && MixHash(ciphertext);
  }

  bool DecryptAndHash(std::span<const std::uint8_t> ciphertext,
                      Bytes &plaintext) {
    return cipher_.Decrypt(h_, ciphertext, plaintext) && MixHash(ciphertext);
  }

  bool Split(CipherState &first, CipherState &second) {
    Hash first_key{};
    Hash second_key{};
    if (!Hkdf2(ck_, {}, first_key, second_key))
      return false;
    first.InitializeKey(first_key);
    second.InitializeKey(second_key);
    Crypt::SecureZero(first_key);
    Crypt::SecureZero(second_key);
    return true;
  }

  const Hash &hash() const { return h_; }

private:
  CipherState cipher_;
  Hash ck_{};
  Hash h_{};
};

bool Dh(const KeyPair &local, const Crypt::X25519PublicKey &remote,
        Hash &output) {
  return Crypt::X25519DeriveSharedSecret(local.secret_key, remote, output);
}

} // namespace

struct Crypt::NoiseXX::Impl {
  Impl(Role requested_role, const KeyPair &requested_static,
       std::span<const std::uint8_t> prologue)
      : role(requested_role), static_key(requested_static) {
    valid = Crypt::Initialize() && symmetric.Initialize(prologue);
  }

  ~Impl() {
    Crypt::SecureZero(static_key.secret_key);
    Crypt::SecureZero(ephemeral.secret_key);
    Crypt::SecureZero(remote_static);
    Crypt::SecureZero(remote_ephemeral);
  }

  bool EnsureEphemeral() {
    if (has_ephemeral)
      return true;
    has_ephemeral = Crypt::GenerateX25519KeyPair(ephemeral);
    return has_ephemeral;
  }

  bool MixDh(const KeyPair &local, const X25519PublicKey &remote) {
    Hash shared{};
    const bool ok = Dh(local, remote, shared) && symmetric.MixKey(shared);
    Crypt::SecureZero(shared);
    return ok;
  }

  bool Complete() {
    if (!symmetric.Split(first, second))
      return false;
    complete = true;
    return true;
  }

  Role role;
  KeyPair static_key{};
  KeyPair ephemeral{};
  X25519PublicKey remote_static{};
  X25519PublicKey remote_ephemeral{};
  SymmetricState symmetric;
  CipherState first;
  CipherState second;
  std::uint8_t message_index = 0;
  bool has_ephemeral = false;
  bool complete = false;
  bool valid = false;
};

Crypt::NoiseXX::NoiseXX(Role role, const X25519KeyPair &static_key,
                        std::span<const std::uint8_t> prologue)
    : impl_(std::make_unique<Impl>(role, static_key, prologue)) {}

Crypt::NoiseXX::~NoiseXX() = default;
Crypt::NoiseXX::NoiseXX(NoiseXX &&) noexcept = default;
Crypt::NoiseXX &Crypt::NoiseXX::operator=(NoiseXX &&) noexcept = default;

#ifdef LIBCRYPT_TESTING
bool Crypt::NoiseXX::SetEphemeralForTesting(const X25519SecretKey &secret_key) {
  if (!impl_ || !impl_->valid || impl_->message_index != 0 ||
      impl_->has_ephemeral)
    return false;
  impl_->has_ephemeral =
      Crypt::X25519KeyPairFromSecret(secret_key, impl_->ephemeral);
  return impl_->has_ephemeral;
}

#endif

bool Crypt::NoiseXX::WriteMessage(std::span<const std::uint8_t> payload,
                                  Bytes &message) {
  message.clear();
  if (!impl_ || !impl_->valid || impl_->complete)
    return false;
  Bytes encrypted;
  Hash shared{};
  bool ok = false;
  if (impl_->role == Role::initiator && impl_->message_index == 0) {
    ok = impl_->EnsureEphemeral();
    if (ok) {
      Append(message, impl_->ephemeral.public_key);
      ok = impl_->symmetric.MixHash(impl_->ephemeral.public_key) &&
           impl_->symmetric.EncryptAndHash(payload, encrypted);
      Append(message, encrypted);
    }
  } else if (impl_->role == Role::responder && impl_->message_index == 1) {
    ok = impl_->EnsureEphemeral();
    if (ok) {
      Append(message, impl_->ephemeral.public_key);
      ok = impl_->symmetric.MixHash(impl_->ephemeral.public_key) &&
           impl_->MixDh(impl_->ephemeral, impl_->remote_ephemeral) &&
           impl_->symmetric.EncryptAndHash(impl_->static_key.public_key,
                                           encrypted);
      Append(message, encrypted);
    }
    if (ok) {
      ok = impl_->MixDh(impl_->static_key, impl_->remote_ephemeral) &&
           impl_->symmetric.EncryptAndHash(payload, encrypted);
      Append(message, encrypted);
    }
  } else if (impl_->role == Role::initiator && impl_->message_index == 2) {
    ok = impl_->symmetric.EncryptAndHash(impl_->static_key.public_key,
                                         encrypted);
    Append(message, encrypted);
    if (ok) {
      ok = impl_->MixDh(impl_->static_key, impl_->remote_ephemeral) &&
           impl_->symmetric.EncryptAndHash(payload, encrypted);
      Append(message, encrypted);
    }
    if (ok)
      ok = impl_->Complete();
  }
  Crypt::SecureZero(shared);
  Crypt::SecureZero(encrypted);
  if (!ok) {
    Crypt::SecureZero(message);
    message.clear();
    impl_->valid = false;
    return false;
  }
  ++impl_->message_index;
  return true;
}

bool Crypt::NoiseXX::ReadMessage(std::span<const std::uint8_t> message,
                                 Bytes &payload) {
  payload.clear();
  if (!impl_ || !impl_->valid || impl_->complete)
    return false;
  bool ok = false;
  Bytes decrypted;
  if (impl_->role == Role::responder && impl_->message_index == 0 &&
      message.size() >= kDhSize) {
    std::copy_n(message.begin(), kDhSize, impl_->remote_ephemeral.begin());
    ok = impl_->symmetric.MixHash(impl_->remote_ephemeral) &&
         impl_->symmetric.DecryptAndHash(message.subspan(kDhSize), payload);
  } else if (impl_->role == Role::initiator && impl_->message_index == 1 &&
             message.size() >= kDhSize + kDhSize + kTagSize + kTagSize) {
    std::size_t offset = 0;
    std::copy_n(message.begin(), kDhSize, impl_->remote_ephemeral.begin());
    offset += kDhSize;
    ok = impl_->symmetric.MixHash(impl_->remote_ephemeral) &&
         impl_->MixDh(impl_->ephemeral, impl_->remote_ephemeral) &&
         impl_->symmetric.DecryptAndHash(
             message.subspan(offset, kDhSize + kTagSize), decrypted) &&
         decrypted.size() == kDhSize;
    if (ok) {
      std::copy(decrypted.begin(), decrypted.end(),
                impl_->remote_static.begin());
      offset += kDhSize + kTagSize;
      ok = impl_->MixDh(impl_->ephemeral, impl_->remote_static) &&
           impl_->symmetric.DecryptAndHash(message.subspan(offset), payload);
    }
  } else if (impl_->role == Role::responder && impl_->message_index == 2 &&
             message.size() >= kDhSize + kTagSize + kTagSize) {
    std::size_t offset = 0;
    ok = impl_->symmetric.DecryptAndHash(
             message.subspan(offset, kDhSize + kTagSize), decrypted) &&
         decrypted.size() == kDhSize;
    if (ok) {
      std::copy(decrypted.begin(), decrypted.end(),
                impl_->remote_static.begin());
      offset += kDhSize + kTagSize;
      ok = impl_->MixDh(impl_->ephemeral, impl_->remote_static) &&
           impl_->symmetric.DecryptAndHash(message.subspan(offset), payload) &&
           impl_->Complete();
    }
  }
  Crypt::SecureZero(decrypted);
  if (!ok) {
    Crypt::SecureZero(payload);
    payload.clear();
    impl_->valid = false;
    return false;
  }
  ++impl_->message_index;
  return true;
}

bool Crypt::NoiseXX::complete() const noexcept {
  return impl_ && impl_->valid && impl_->complete;
}

bool Crypt::NoiseXX::GetHandshakeHash(Hash256 &output) const {
  if (!complete())
    return false;
  output = impl_->symmetric.hash();
  return true;
}

bool Crypt::NoiseXX::EncryptTransport(std::span<const std::uint8_t> plaintext,
                                      Bytes &ciphertext) {
  if (!complete())
    return false;
  CipherState &sender =
      impl_->role == Role::initiator ? impl_->first : impl_->second;
  return sender.Encrypt({}, plaintext, ciphertext);
}

bool Crypt::NoiseXX::DecryptTransport(std::span<const std::uint8_t> ciphertext,
                                      Bytes &plaintext) {
  if (!complete())
    return false;
  CipherState &receiver =
      impl_->role == Role::initiator ? impl_->second : impl_->first;
  return receiver.Decrypt({}, ciphertext, plaintext);
}
