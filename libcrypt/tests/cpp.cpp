#include <cstdio>
#include <libcrypt/crypt.hpp>
using devkit::Crypt;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "C++ check failed at %d: %s\n", __LINE__, #x);      \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main() {
  Crypt::Bytes in{1, 2, 3}, cipher, restored;
  Crypt::Ed25519KeyPair sign;
  CHECK(Crypt::GenerateEd25519KeyPair(sign));
  Crypt::Ed25519Signature signature;
  CHECK(Crypt::Ed25519Sign(in, sign.secret_key, signature));
  CHECK(Crypt::Ed25519Verify(in, signature, sign.public_key));
  signature[0] ^= 1;
  CHECK(!Crypt::Ed25519Verify(in, signature, sign.public_key));
  Crypt::Ed25519Seed seed{};
  Crypt::Ed25519PublicKey public_key{};
  CHECK(Crypt::Ed25519PublicFromSeed(seed, public_key));
  CHECK(Crypt::Ed25519SignFromSeed(seed, in, signature));
  CHECK(Crypt::Ed25519Verify(in, signature, public_key));
  Crypt::X25519KeyPair a, b;
  CHECK(Crypt::GenerateX25519KeyPair(a) && Crypt::GenerateX25519KeyPair(b));
  Crypt::X25519SharedSecret ab{}, ba{};
  CHECK(Crypt::X25519DeriveSharedSecret(a.secret_key, b.public_key, ab));
  CHECK(Crypt::X25519DeriveSharedSecret(b.secret_key, a.public_key, ba));
  CHECK(ab == ba);
  CHECK(Crypt::Seal(in, a.public_key, cipher) &&
        Crypt::OpenSealed(cipher, a, restored) && in == restored);
  cipher.back() ^= 1;
  CHECK(!Crypt::OpenSealed(cipher, a, restored) && restored.empty());
  std::string text;
  CHECK(Crypt::Base64Encode(in, text) && Crypt::Base64Decode(text, restored) &&
        in == restored);
  CHECK(Crypt::Base58Decode(Crypt::Base58Encode(in), restored) &&
        in == restored);
  Crypt::Sha256Stream stream;
  CHECK(stream.Update(in));
  Crypt::Sha256Stream moved(std::move(stream));
  Crypt::Hash256 h{}, expected{};
  CHECK(!stream.Update(in) && moved.Finalize(h) &&
        Crypt::Sha256(in, expected) && h == expected);
  Crypt::NoiseXX init(Crypt::NoiseXX::Role::initiator, a),
      resp(Crypt::NoiseXX::Role::responder, b);
  CHECK(init.WriteMessage({}, cipher) && resp.ReadMessage(cipher, restored));
  CHECK(resp.WriteMessage({}, cipher) && init.ReadMessage(cipher, restored));
  CHECK(init.WriteMessage({}, cipher) && resp.ReadMessage(cipher, restored));
  CHECK(init.GetHandshakeHash(h) && resp.GetHandshakeHash(expected) &&
        h == expected);
  CHECK(init.EncryptTransport(in, cipher) &&
        resp.DecryptTransport(cipher, restored) && restored == in);
  CHECK(!resp.DecryptTransport(cipher, restored) && restored.empty());
  Crypt::SecureZero(sign.secret_key);
  Crypt::SecureZero(a.secret_key);
  Crypt::SecureZero(b.secret_key);
  Crypt::SecureZero(ab);
  Crypt::SecureZero(ba);
  std::puts(
      "PASS C++ convenience layer, signing, agreement, sealed boxes and Noise");
  return 0;
}
