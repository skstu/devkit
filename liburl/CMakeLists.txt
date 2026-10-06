cmake_minimum_required(VERSION 3.21)

# Header-only URL parsing (Chromium GURL-style API) backed by uriparser.
# Split out of dispatch so it can grow its own web3-facing helpers (e.g.
# EIP-681 / WalletConnect URI parsing) without dragging in libpath.
find_package(uriparser CONFIG REQUIRED)

add_library(liburl INTERFACE)
add_library(sovrankit::liburl ALIAS liburl)
target_sources(liburl INTERFACE include/liburl/gurl.hpp)
target_include_directories(liburl INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(liburl INTERFACE cxx_std_20)
target_link_libraries(liburl INTERFACE uriparser::uriparser)
