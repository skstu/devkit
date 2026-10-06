cmake_minimum_required(VERSION 3.21)

# Web3-oriented cryptographic primitives backed by libsodium.  The public API
# intentionally exposes byte containers rather than libsodium types, keeping
# consumers independent from the implementation library.
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(libcrypt STATIC
  include/libcrypt.h
  src/libcrypt.cc
  src/libcrypt_stream.cc
  src/libcrypt_noise.cc
  src/libcrypt_openssl.cc)
add_library(sovrankit::libcrypt ALIAS libcrypt)

set_target_properties(libcrypt PROPERTIES
  PREFIX ""
  POSITION_INDEPENDENT_CODE TRUE)
target_compile_features(libcrypt PUBLIC cxx_std_20)

target_include_directories(libcrypt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(libcrypt PRIVATE unofficial-sodium::sodium OpenSSL::Crypto)

sovrankit_set_warnings(libcrypt)

# Deterministic vectors compile a separate copy; production always generates
# its own ephemeral keys and does not export the injection method.
if(BUILD_TESTING)
  get_target_property(libcrypt_sources libcrypt SOURCES)
  add_library(libcrypt_testing STATIC EXCLUDE_FROM_ALL ${libcrypt_sources})
  target_compile_features(libcrypt_testing PUBLIC cxx_std_20)
  target_compile_definitions(libcrypt_testing PUBLIC LIBCRYPT_TESTING=1)
  target_include_directories(libcrypt_testing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(libcrypt_testing PRIVATE unofficial-sodium::sodium OpenSSL::Crypto)
  sovrankit_set_warnings(libcrypt_testing)
endif()
