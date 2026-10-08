#include <libcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

static_assert(noexcept(Crypt::ConstantTimeEqual({}, {})));

Crypt::Bytes Hex(std::string_view text) {
  Crypt::Bytes output;
  output.reserve(text.size() / 2);
  auto nibble = [](char value) -> std::uint8_t {
    if (value >= '0' && value <= '9')
      return static_cast<std::uint8_t>(value - '0');
    return static_cast<std::uint8_t>(value - 'a' + 10);
  };
  for (std::size_t index = 0; index < text.size(); index += 2)
    output.push_back(static_cast<std::uint8_t>((nibble(text[index]) << 4) |
                                               nibble(text[index + 1])));
  return output;
}

template <typename Array> Array HexArray(std::string_view text) {
  const Crypt::Bytes bytes = Hex(text);
  Array output{};
  if (bytes.size() == output.size())
    std::copy(bytes.begin(), bytes.end(), output.begin());
  return output;
}

std::string HexText(std::span<const std::uint8_t> bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output(bytes.size() * 2, '0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    output[index * 2] = digits[bytes[index] >> 4];
    output[index * 2 + 1] = digits[bytes[index] & 15];
  }
  return output;
}

bool CheckNoiseXXVector() {
  // noise-c upstream vector for Noise_XX_25519_ChaChaPoly_SHA256.
  const auto initiator_static = HexArray<Crypt::X25519SecretKey>(
      "e61ef9919cde45dd5f82166404bd08e38bceb5dfdfded0a34c8df7ed542214d1");
  const auto responder_static = HexArray<Crypt::X25519SecretKey>(
      "4a3acbfdb163dec651dfa3194dece676d437029c62a408b4c5ea9114246e4893");
  const auto initiator_ephemeral = HexArray<Crypt::X25519SecretKey>(
      "893e28b9dc6ca8d611ab664754b8ceb7bac5117349a4439a6b0569da977c464a");
  const auto responder_ephemeral = HexArray<Crypt::X25519SecretKey>(
      "bbdb4cdbd309f1a1f2e1456967fe288cadd6f712d65dc7b7793d5e63da6b375b");
  Crypt::X25519KeyPair initiator_key;
  Crypt::X25519KeyPair responder_key;
  if (!Crypt::X25519KeyPairFromSecret(initiator_static, initiator_key) ||
      !Crypt::X25519KeyPairFromSecret(responder_static, responder_key)) {
    std::cerr << "Noise static key setup failed\n";
    return false;
  }
  const Crypt::Bytes prologue = Hex("50726f6c6f677565313233");
  Crypt::NoiseXX initiator(Crypt::NoiseXX::Role::initiator, initiator_key,
                           prologue);
  Crypt::NoiseXX responder(Crypt::NoiseXX::Role::responder, responder_key,
                           prologue);
  if (!initiator.SetEphemeralForTesting(initiator_ephemeral) ||
      !responder.SetEphemeralForTesting(responder_ephemeral)) {
    std::cerr << "Noise ephemeral key setup failed\n";
    return false;
  }

  const std::array payloads = {Hex("4c756477696720766f6e204d69736573"),
                               Hex("4d757272617920526f746862617264"),
                               Hex("462e20412e20486179656b")};
  const std::array expected = {
      Hex("ca35def5ae56cec33dc2036731ab14896bc4c75dbb07a61f879f8e3afa4c79444c75"
          "6477696720766f6e204d69736573"),
      Hex("95ebc60d2b1fa672c1f46a8aa265ef51bfe38e7ccb39ec5be34069f14480884381cb"
          "ad1f276e038c48378ffce2b65285e08d6b68aaa3629a5a8639392490e5b94a6d8798"
          "832d5372f220f161d9c2df035528f8982ffe09be9b5c412f8a0db5d21351f20af137"
          "0d0bf8ef1a8c59a30e"),
      Hex("c7195ffacac1307ff99046f219750fc47693e23c3cb08b89c2af808b444850a85899"
          "81dfbd651e6ff4724a781cc2aa6158c9fea0d4ec82a286427484c5b8c8123a7a6002"
          "b1de9f9775fc97")};
  Crypt::Bytes message;
  Crypt::Bytes restored;
  if (!initiator.WriteMessage(payloads[0], message)) {
    std::cerr << "Noise write message 1 failed\n";
    return false;
  }
  if (message != expected[0]) {
    std::cerr << "Noise message 1 actual: " << HexText(message) << '\n';
    return false;
  }
  if (!responder.ReadMessage(message, restored) || restored != payloads[0] ||
      !responder.WriteMessage(payloads[1], message)) {
    std::cerr << "Noise process message 2 failed\n";
    return false;
  }
  if (message != expected[1]) {
    std::cerr << "Noise message 2 actual: " << HexText(message) << '\n';
    return false;
  }
  if (!initiator.ReadMessage(message, restored) || restored != payloads[1] ||
      !initiator.WriteMessage(payloads[2], message))
    return false;
  if (message != expected[2]) {
    std::cerr << "Noise message 3 actual: " << HexText(message) << '\n';
    return false;
  }
  if (!responder.ReadMessage(message, restored) || restored != payloads[2] ||
      !initiator.complete() || !responder.complete())
    return false;

  const Crypt::Bytes transport_payload = Hex("4361726c204d656e676572");
  const Crypt::Bytes transport_expected =
      Hex("96763ed773f8e47bb3712f0e29b3060ffc956ffc146cee53d5e1df");
  if (!responder.EncryptTransport(transport_payload, message))
    return false;
  if (message != transport_expected) {
    std::cerr << "Noise transport actual: " << HexText(message) << '\n';
    return false;
  }
  return initiator.DecryptTransport(message, restored) &&
         restored == transport_payload;
}

} // namespace

int main() { return CheckNoiseXXVector() ? 0 : 1; }
