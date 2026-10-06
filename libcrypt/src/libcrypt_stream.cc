#include <libcrypt.h>

#include <limits>
#include <utility>

namespace {
Crypt::XChaCha20Poly1305Nonce Nonce(std::uint64_t stream,
                                    std::uint64_t sequence) {
  Crypt::XChaCha20Poly1305Nonce nonce{'S', 'V', 'K', 'N', 'E', 'T', '2', 0};
  for (int i = 0; i < 8; ++i) {
    nonce[8 + i] = static_cast<std::uint8_t>(stream >> (56 - 8 * i));
    nonce[16 + i] = static_cast<std::uint8_t>(sequence >> (56 - 8 * i));
  }
  return nonce;
}
} // namespace

Crypt::StreamCipher::StreamCipher(const XChaCha20Poly1305Key &key,
                                  Bytes context, std::uint64_t stream)
    : key_(key), context_(std::move(context)), stream_(stream) {}
Crypt::StreamCipher::~StreamCipher() { SecureZero(key_); }
bool Crypt::StreamCipher::Encrypt(std::span<const std::uint8_t> plaintext,
                                  Bytes &record) {
  record.clear();
  if (plaintext.empty() || plaintext.size() > 32700 || !stream_ ||
      send_ == std::numeric_limits<std::uint64_t>::max())
    return false;
  const auto nonce = Nonce(stream_, send_);
  Bytes encrypted;
  if (!XChaCha20Poly1305Encrypt(plaintext, context_, key_, nonce, encrypted))
    return false;
  record.assign(nonce.begin() + 16, nonce.end());
  record.insert(record.end(), encrypted.begin(), encrypted.end());
  ++send_;
  return true;
}
bool Crypt::StreamCipher::Decrypt(std::span<const std::uint8_t> record,
                                  Bytes &plaintext) {
  plaintext.clear();
  if (record.size() <= 24 || record.size() > 32724 || !stream_ ||
      receive_ == std::numeric_limits<std::uint64_t>::max())
    return false;
  const auto nonce = Nonce(stream_, receive_);
  if (!ConstantTimeEqual(record.first(8), std::span(nonce).subspan(16)) ||
      !XChaCha20Poly1305Decrypt(record.subspan(8), context_, key_, nonce,
                                plaintext))
    return false;
  ++receive_;
  return true;
}
