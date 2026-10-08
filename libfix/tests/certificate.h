// Isolated certificate fixture extracted from tdbrg (Apache-2.0).
#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x))                                                                                      \
      throw std::runtime_error(#x);                                                                \
  } while (0)
struct Certificate {
  std::filesystem::path folder, cert, key;
  Certificate() {
    folder = std::filesystem::temp_directory_path() /
             ("devkit-libfix-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(folder));
    cert = folder / "cert.pem";
    key = folder / "key.pem";
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    CHECK(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1);
    CHECK(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) == 1);
    EVP_PKEY *raw = nullptr;
    CHECK(EVP_PKEY_keygen(ctx.get(), &raw) == 1);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(raw, EVP_PKEY_free);
    std::unique_ptr<X509, decltype(&X509_free)> x509(X509_new(), X509_free);
    CHECK(x509 && X509_set_version(x509.get(), 2) == 1);
    CHECK(ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1) == 1);
    CHECK(X509_gmtime_adj(X509_getm_notBefore(x509.get()), -60));
    CHECK(X509_gmtime_adj(X509_getm_notAfter(x509.get()), 3600));
    CHECK(X509_set_pubkey(x509.get(), pkey.get()) == 1);
    auto *name = X509_get_subject_name(x509.get());
    CHECK(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                     reinterpret_cast<const unsigned char *>("localhost"), -1, -1,
                                     0) == 1);
    CHECK(X509_set_issuer_name(x509.get(), name) == 1);
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> san(
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "IP:127.0.0.1"),
        X509_EXTENSION_free);
    CHECK(san && X509_add_ext(x509.get(), san.get(), -1) == 1);
    CHECK(X509_sign(x509.get(), pkey.get(), EVP_sha256()) > 0);
    std::unique_ptr<FILE, decltype(&fclose)> c(fopen(cert.c_str(), "w"), fclose),
        k(fopen(key.c_str(), "w"), fclose);
    CHECK(c && k);
    CHECK(PEM_write_X509(c.get(), x509.get()) == 1);
    CHECK(PEM_write_PrivateKey(k.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1);
  }
  ~Certificate() { std::filesystem::remove_all(folder); }
};
