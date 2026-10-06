cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NativeLibsBuild.cmake")

# Both modes share definitions; choose one mode throughout a binary.
option(LIBSTL_HEADER_ONLY "Use inline libstl implementations instead of a static library" OFF)
find_package(Threads REQUIRED)

# Pure STL templates, without libpath/Iconv dependencies.
add_library(libstl_core INTERFACE)
add_library(sovrankit::libstl_core ALIAS libstl_core)
target_compile_features(libstl_core INTERFACE cxx_std_20)
target_include_directories(libstl_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(libstl_core INTERFACE Threads::Threads)

if(NOT TARGET sovrankit::libpath)
  return()
endif()

add_library(libstl_header_only INTERFACE)
add_library(sovrankit::libstl_header_only ALIAS libstl_header_only)
target_link_libraries(libstl_header_only INTERFACE libstl_core sovrankit::libpath)
target_compile_definitions(libstl_header_only INTERFACE LIBSTL_HEADER_ONLY=1)

if(LIBSTL_HEADER_ONLY)
  add_library(libstl INTERFACE)
  target_link_libraries(libstl INTERFACE libstl_header_only)
else()
  add_library(libstl STATIC src/libstl.cc src/packet.cc src/time.cc src/utils.cc)
  set_target_properties(libstl PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE TRUE)
  target_link_libraries(libstl PUBLIC libstl_core sovrankit::libpath)
  target_compile_definitions(libstl PUBLIC LIBSTL_HEADER_ONLY=0)
  native_libs_set_warnings(libstl)
endif()
add_library(sovrankit::libstl ALIAS libstl)
