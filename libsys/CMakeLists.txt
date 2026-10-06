cmake_minimum_required(VERSION 3.21)

project(libsys LANGUAGES CXX)

include(GNUInstallDirs)

# Small components are usable without the legacy platform-probing facade.
# Keep compile/install policy identical across all components.
function(libsys_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  if(COMMAND sovkit_set_warnings)
    sovkit_set_warnings(${target})
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

if(NOT COMMAND sovkit_manifest_get_list)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/SovkitBuildManifest.cmake")
  sovkit_load_build_manifest(
    "${CMAKE_CURRENT_SOURCE_DIR}/../../projects/sdk/build.json")
endif()

# The SDK needs only system-directory resolution; do not pull GPU, proxy or
# process-enumeration dependencies into every mobile build.
add_library(libsys_directories STATIC src/directories.cc)
add_library(sovrankit::libsys_directories ALIAS libsys_directories)
libsys_configure_target(libsys_directories)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(libsys_directories PRIVATE src/apple/directories.mm)
  set_source_files_properties(src/apple/directories.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(libsys_directories PRIVATE "-framework Foundation")
elseif(WIN32)
  target_link_libraries(libsys_directories PRIVATE shell32 ole32)
endif()
find_package(Threads REQUIRED)
add_library(libsys_file_lock STATIC src/file_lock.cc)
add_library(sovrankit::libsys_file_lock ALIAS libsys_file_lock)
libsys_configure_target(libsys_file_lock)

add_library(libsys_console STATIC EXCLUDE_FROM_ALL src/console_input.cc)
add_library(sovrankit::libsys_console ALIAS libsys_console)
libsys_configure_target(libsys_console)
target_link_libraries(libsys_console PUBLIC Threads::Threads)
if(WIN32)
  target_sources(libsys_console PRIVATE src/windows/console_input.cc)
elseif(UNIX)
  target_sources(libsys_console PRIVATE src/posix/console_input.cc)
else()
  message(FATAL_ERROR "libsys console does not support ${CMAKE_SYSTEM_NAME}")
endif()

set(LIBSYS_FILE_IO_DEFAULT OFF)
if(TARGET libuv::uv_a OR TARGET libuv::uv OR SOVKIT_BUILD_CORE)
  set(LIBSYS_FILE_IO_DEFAULT ON)
endif()
option(LIBSYS_ENABLE_FILE_IO "Build libuv-backed file IO and resource observation" "${LIBSYS_FILE_IO_DEFAULT}")
if(LIBSYS_ENABLE_FILE_IO)
  find_package(libuv CONFIG REQUIRED)
  if(TARGET libuv::uv_a)
    set(LIBSYS_UV libuv::uv_a)
  else()
    set(LIBSYS_UV libuv::uv)
  endif()
  add_library(libsys_file_io STATIC src/file_io.cc src/resources.cc)
  add_library(sovrankit::libsys_file_io ALIAS libsys_file_io)
  libsys_configure_target(libsys_file_io)
  target_link_libraries(libsys_file_io PUBLIC ${LIBSYS_UV})
  install(TARGETS libsys_file_io ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

option(LIBSYS_COMPONENTS_ONLY "Build only the small libsys directory/console/file-lock components" OFF)
if(LIBSYS_COMPONENTS_ONLY)
  set_target_properties(libsys_console PROPERTIES EXCLUDE_FROM_ALL OFF)
  install(TARGETS libsys_directories libsys_console libsys_file_lock ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  return()
endif()
if(DEFINED SOVKIT_BUILD_LIBSYS AND NOT SOVKIT_BUILD_LIBSYS
   AND NOT SOVKIT_BUILD_SDK_CONSUMER)
  return()
endif()

find_package(nlohmann_json CONFIG REQUIRED)
if(NOT TARGET sovrankit::libstl_core)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../libstl"
                   "${CMAKE_CURRENT_BINARY_DIR}/libstl")
endif()

sovkit_manifest_get_list(
  LIBSYS_SOURCES targets libsys commonSources)

if(WIN32)
  set(LIBSYS_PLATFORM windows)
elseif(APPLE)
  enable_language(OBJCXX)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(LIBSYS_PLATFORM ios)
  else()
    set(LIBSYS_PLATFORM macos)
  endif()
elseif(ANDROID)
  set(LIBSYS_PLATFORM android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LIBSYS_PLATFORM linux)
else()
  message(FATAL_ERROR "libsys does not yet support ${CMAKE_SYSTEM_NAME}")
endif()
sovkit_manifest_get_list(
  LIBSYS_PLATFORM_SOURCES targets libsys platforms ${LIBSYS_PLATFORM} sources)
list(APPEND LIBSYS_SOURCES ${LIBSYS_PLATFORM_SOURCES})
list(REMOVE_ITEM LIBSYS_SOURCES "3rdparty/libsys/src/directories.cc"
  "3rdparty/libsys/src/file_lock.cc"
  "3rdparty/libsys/src/apple/directories.mm" "3rdparty/libsys/src/console_input.cc"
  "3rdparty/libsys/src/windows/console_input.cc" "3rdparty/libsys/src/posix/console_input.cc")
sovkit_manifest_make_absolute(
  LIBSYS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../.." ${LIBSYS_SOURCES})
sovkit_manifest_get_list(
  LIBSYS_SYSTEM_LIBRARIES
  targets libsys systemLibraries cmake ${LIBSYS_PLATFORM})
sovkit_manifest_get_list(
  LIBSYS_INCLUDE_DIRECTORIES targets libsys includeDirectories)
sovkit_manifest_make_absolute(
  LIBSYS_INCLUDE_DIRECTORIES
  "${CMAKE_CURRENT_SOURCE_DIR}/../.."
  ${LIBSYS_INCLUDE_DIRECTORIES})

add_library(libsys STATIC ${LIBSYS_SOURCES})
add_library(sovrankit::libsys ALIAS libsys)
libsys_configure_target(libsys)

set_target_properties(
  libsys PROPERTIES
  CXX_EXTENSIONS OFF
  OUTPUT_NAME sys
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  # Keep the portable codecvt conversion until its cross-platform replacement;
  # suppress only that standard-library deprecation within this private target.
  target_compile_definitions(libsys PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
endif()
target_include_directories(
  libsys
  PRIVATE ${LIBSYS_INCLUDE_DIRECTORIES})
target_link_libraries(
  libsys
  PUBLIC Threads::Threads sovrankit::libsys_directories sovrankit::libsys_console sovrankit::libsys_file_lock
  PRIVATE nlohmann_json::nlohmann_json sovrankit::libstl_core)

if(WIN32)
  target_link_libraries(libsys PRIVATE ${LIBSYS_SYSTEM_LIBRARIES})
elseif(APPLE)
  set_source_files_properties(
    src/apple/activation.mm src/apple/egress.mm src/apple/gpu.mm src/apple/directories.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_options(libsys PRIVATE -Wno-deprecated-declarations)
  target_link_libraries(libsys PRIVATE "-framework Foundation" "-framework Security")
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(
      libsys PRIVATE
      "-framework IOKit"
      "-framework Metal"
      "-framework AppKit"
      "-framework Foundation"
      "-framework CoreFoundation"
      "-framework SystemConfiguration"
      "-framework Security"
      "-framework ApplicationServices")
  endif()
elseif(ANDROID)
  target_link_libraries(libsys PRIVATE ${LIBSYS_SYSTEM_LIBRARIES})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(libsys PRIVATE ${LIBSYS_SYSTEM_LIBRARIES})
endif()

# A consumer test needs libsys locally, without expanding the SDK package.
if(NOT DEFINED SOVKIT_BUILD_LIBSYS OR SOVKIT_BUILD_LIBSYS)
  install(
    TARGETS libsys libsys_directories libsys_console libsys_file_lock
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
