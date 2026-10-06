#include <libcrypt.h>

#include <openssl/evp.h>

namespace {

const std::uint8_t kEmptyInput = 0;

const std::uint8_t *DataOrEmpty(std::span<const std::uint8_t> input) {
  return input.empty() ? &kEmptyInput : input.data();
}

} // namespace

bool Crypt::Md5(std::span<const std::uint8_t> input, Hash128 &output) {
  std::size_t length = 0;
  return EVP_Q_digest(nullptr, "MD5", nullptr, DataOrEmpty(input), input.size(),
                      output.data(), &length) == 1 &&
         length == output.size();
}
