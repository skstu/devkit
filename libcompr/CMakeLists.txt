cmake_minimum_required(VERSION 3.21)

add_library(libcompr_stream INTERFACE)
add_library(sovrankit::libcompr_stream ALIAS libcompr_stream)
target_compile_features(libcompr_stream INTERFACE cxx_std_20)
target_include_directories(libcompr_stream INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(LIBCOMPR_STREAM_ONLY)
  return()
endif()


# Archive / stream (de)compression: zip via minizip+zlib, gzip via zlib,
# zstd, brotli, and generic archive extraction via libarchive. Public API
# (libcompr.h) exposes stl::path, so libstl is a PUBLIC dependency.
find_package(minizip CONFIG REQUIRED)

if(TARGET MINIZIP::minizip)
  set(LIBCOMPR_MINIZIP_TARGET MINIZIP::minizip)
elseif(TARGET MINIZIP::minizipstatic)
  set(LIBCOMPR_MINIZIP_TARGET MINIZIP::minizipstatic)
else()
  message(FATAL_ERROR "minizip did not export a supported CMake target")
endif()

add_library(libcompr STATIC
  include/libcompr.h
  src/archive.cc
  src/compress.cc
  src/minizip.cc)
add_library(sovrankit::libcompr ALIAS libcompr)

set_target_properties(libcompr PROPERTIES
  PREFIX ""
  POSITION_INDEPENDENT_CODE TRUE)
target_compile_features(libcompr PUBLIC cxx_std_20)

target_include_directories(libcompr PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(libcompr
  PUBLIC
    sovrankit::libstl
  PRIVATE
    ZLIB::ZLIB
    ${LIBCOMPR_MINIZIP_TARGET}
    zstd::libzstd_static
    sovrankit::brotli
    LibArchive::LibArchive)

sovrankit_set_warnings(libcompr)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  # Compression backends expose C byte buffers and retain their ABI casts.
  target_compile_options(libcompr PRIVATE -Wno-old-style-cast
                                            -Wno-unknown-pragmas)
endif()
