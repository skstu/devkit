cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(libnet LANGUAGES C CXX)
endif()

include(GNUInstallDirs)

set(LIBNET_ENABLE_HTTP_DEFAULT ON)
if(DEFINED SOVKIT_BUILD_LIBNET)
  set(LIBNET_ENABLE_HTTP_DEFAULT "${SOVKIT_BUILD_LIBNET}")
endif()
option(LIBNET_ENABLE_HTTP "Build the HTTP client and downloader" "${LIBNET_ENABLE_HTTP_DEFAULT}")
option(LIBNET_ENABLE_WEBSOCKETS "Build the optional WebSocket backends" OFF)

if(LIBNET_ENABLE_HTTP OR SOVKIT_BUILD_CORE)
find_package(CURL REQUIRED)
add_library(libnet_http STATIC src/libnet_http_client.cc)
add_library(sovrankit::libnet_http ALIAS libnet_http)
target_compile_features(libnet_http PUBLIC cxx_std_20)
set_target_properties(libnet_http PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(libnet_http PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(libnet_http PRIVATE CURL::libcurl)
install(TARGETS libnet_http ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/libnet_http_client.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

# HTTP consumers can reuse the existing client without ICE/QUIC dependencies.
option(LIBNET_HTTP_ONLY "Build only the synchronous HTTP client" OFF)
if(LIBNET_HTTP_ONLY)
  if(NOT TARGET libnet_http)
    message(FATAL_ERROR "LIBNET_HTTP_ONLY requires LIBNET_ENABLE_HTTP")
  endif()
  add_library(libnet INTERFACE)
  target_link_libraries(libnet INTERFACE libnet_http)
  add_library(sovrankit::libnet ALIAS libnet)
  return()
endif()

include(cmake/Uv.cmake)
find_package(LibJuice CONFIG REQUIRED)
find_package(ngtcp2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(libnet_transport STATIC
  include/libnet_assistance_node.h
  src/libnet_assistance_node.cc
  include/libnet_tcp_direct.h
  src/libnet_tcp_direct.cc
  include/libnet_stun_server.h
  src/libnet_stun_server.cc
  include/libnet_ice.h
  src/libnet_ice.cc
  include/libnet_ice_transport.h
  src/libnet_ice_transport.cc
  include/libnet_network_path.h
  src/libnet_network_path.cc
  include/libnet_quic.h
  src/libnet_quic.cc
  include/libnet_transport_provider.h
  src/libnet_transport_provider.cc
  include/libnet_uv.h
)
add_library(sovrankit::libnet_transport ALIAS libnet_transport)

set_target_properties(libnet_transport PROPERTIES
  PREFIX ""
  POSITION_INDEPENDENT_CODE TRUE)
target_compile_features(libnet_transport PUBLIC cxx_std_20)

target_include_directories(libnet_transport PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(TARGET libuv::uv)
  set(LIBNET_LIBUV_TARGET libuv::uv)
elseif(TARGET libuv::uv_a)
  set(LIBNET_LIBUV_TARGET libuv::uv_a)
else()
  message(FATAL_ERROR "libnet requires libuv::uv or libuv::uv_a")
endif()

target_link_libraries(
  libnet_transport
  PUBLIC libnet_uv LibJuice::LibJuice
  PRIVATE ngtcp2::ngtcp2_static ngtcp2::ngtcp2_crypto_ossl_static nlohmann_json::nlohmann_json)
if(ANDROID)
  target_link_libraries(libnet_transport PRIVATE android)
elseif(WIN32)
  target_link_libraries(libnet_transport PRIVATE iphlpapi)
endif()


if(LIBNET_ENABLE_HTTP)
  add_library(libnet STATIC
    include/libnet_downloader.h
    include/libnet_http_client.h
    src/libnet_downloader.cc)
  target_compile_features(libnet PUBLIC cxx_std_20)
  target_include_directories(libnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_link_libraries(libnet
    PRIVATE CURL::libcurl
    PUBLIC libnet_transport libnet_http)
else()
  add_library(libnet INTERFACE)
  target_link_libraries(libnet INTERFACE libnet_transport)
endif()
add_library(sovrankit::libnet ALIAS libnet)

if(LIBNET_ENABLE_WEBSOCKETS)
  if(NOT LIBNET_ENABLE_HTTP)
    message(FATAL_ERROR "LIBNET_ENABLE_WEBSOCKETS requires LIBNET_ENABLE_HTTP")
  endif()
  add_library(sovrankit::libws ALIAS libnet)
  find_package(unofficial-uwebsockets CONFIG REQUIRED)
  find_package(unofficial-usockets CONFIG REQUIRED)
  find_package(libhv CONFIG REQUIRED)
  target_sources(libnet PRIVATE
    include/libnet_websocket.h
    include/libnet_ws_client.h
    include/libnet_ws_server.h
    src/libnet_websocket.cc
    src/libnet_ws_client.cc
    src/libnet_ws_server.cc)
  target_link_libraries(libnet PRIVATE
    unofficial::uwebsockets::uwebsockets
    unofficial::usockets::usockets
    hv_static)
endif()

if(COMMAND sovrankit_set_warnings)
  sovrankit_set_warnings(libnet_transport)
  if(LIBNET_ENABLE_HTTP)
    sovrankit_set_warnings(libnet)
  endif()
elseif(MSVC)
  target_compile_options(libnet_transport PRIVATE /W4 /permissive-)
  if(LIBNET_ENABLE_HTTP)
    target_compile_options(libnet PRIVATE /W4 /permissive-)
  endif()
else()
  target_compile_options(libnet_transport PRIVATE -Wall -Wextra -Wpedantic)
  if(LIBNET_ENABLE_HTTP)
    target_compile_options(libnet PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()
if(NOT MSVC AND LIBNET_ENABLE_HTTP)
  # libhv's WebSocketClient headers trigger -Wdeprecated-declarations via
  # sprintf(); not actionable from this side of the dependency.
  target_compile_options(libnet PRIVATE -Wno-deprecated-declarations)
endif()

install(TARGETS libnet_transport ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
if(LIBNET_ENABLE_HTTP)
  install(TARGETS libnet ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(
  FILES include/libnet_ice.h include/libnet_ice_transport.h include/libnet_tcp_direct.h include/libnet_quic.h include/libnet_transport_provider.h
        include/libnet_uv.h include/libnet_stun_server.h include/libnet_network_path.h include/libnet_assistance_node.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(LIBNET_ENABLE_HTTP)
  install(
    FILES include/libnet_downloader.h include/libnet_http_client.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
if(LIBNET_ENABLE_WEBSOCKETS)
  install(
    FILES
      include/libnet_websocket.h
      include/libnet_ws_client.h
      include/libnet_ws_server.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
