cmake_minimum_required(VERSION 3.21)

project(libuvbrg LANGUAGES CXX)

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(libuv CONFIG QUIET)
if(TARGET libuv::uv_a)
  set(LIBUVBRG_UV libuv::uv_a)
elseif(TARGET libuv::uv)
  set(LIBUVBRG_UV libuv::uv)
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libuvbrg_uv REQUIRED IMPORTED_TARGET libuv)
  set(LIBUVBRG_UV PkgConfig::libuvbrg_uv)
else()
  message(FATAL_ERROR "libuvbrg requires libuv")
endif()
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)


add_library(libuvbrg_loop STATIC src/event_loop.cc)
add_library(libuvbrg::loop ALIAS libuvbrg_loop)
set_target_properties(libuvbrg_loop PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(libuvbrg_loop PUBLIC cxx_std_20)
target_include_directories(libuvbrg_loop PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libuvbrg_loop PUBLIC ${LIBUVBRG_UV} Threads::Threads)
install(TARGETS libuvbrg_loop ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/libuvbrg DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

add_library(
  libuvbrg STATIC
  src/libbridge.cc
  src/shared_reactor.cc)
add_library(libuvbrg::libuvbrg ALIAS libuvbrg)

set_target_properties(
  libuvbrg PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME uvbrg)

target_include_directories(
  libuvbrg
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(
  libuvbrg
  PRIVATE OpenSSL::Crypto nlohmann_json::nlohmann_json
  PUBLIC ${LIBUVBRG_UV} Threads::Threads)

if(MSVC)
  target_compile_options(libuvbrg PRIVATE /W3 /wd4267 /wd4996)
  target_link_libraries(libuvbrg PRIVATE ws2_32 iphlpapi userenv psapi advapi32)
else()
  target_compile_options(libuvbrg PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(
  TARGETS libuvbrg
  EXPORT libuvbrgTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(
  FILES include/libuvbrg.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
  EXPORT libuvbrgTargets
  FILE libuvbrgTargets.cmake
  NAMESPACE libuvbrg::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libuvbrg")

# Fault injection exists only in this independent executable, never libuvbrg.
if(BUILD_TESTING)
  add_executable(libuvbrg_reactor_test tests/reactor_shutdown.cpp src/shared_reactor.cc)
  target_compile_features(libuvbrg_reactor_test PRIVATE cxx_std_20)
  target_compile_definitions(libuvbrg_reactor_test PRIVATE LIBUVBRG_TESTING=1)
  target_include_directories(libuvbrg_reactor_test PRIVATE src)
  target_link_libraries(libuvbrg_reactor_test PRIVATE ${LIBUVBRG_UV} Threads::Threads)
  add_test(NAME libuvbrg.reactor_shutdown COMMAND libuvbrg_reactor_test)
  set_tests_properties(libuvbrg.reactor_shutdown PROPERTIES TIMEOUT 15)
endif()
