cmake_minimum_required(VERSION 3.21)

# Standard filesystem UTF-8 paths do not require legacy encoding libraries.
add_library(libpath_utf8 INTERFACE)
add_library(sovrankit::libpath_utf8 ALIAS libpath_utf8)
target_include_directories(libpath_utf8 INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(libpath_utf8 INTERFACE cxx_std_20)
if(LIBPATH_UTF8_ONLY)
  return()
endif()
# Optional explicit legacy-encoding bridge.
find_package(Iconv REQUIRED)
add_library(libpath INTERFACE)
add_library(sovrankit::libpath ALIAS libpath)
target_sources(libpath INTERFACE include/libpath/path.hpp)
target_include_directories(libpath INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(libpath INTERFACE cxx_std_20)
target_link_libraries(libpath INTERFACE Iconv::Iconv)
