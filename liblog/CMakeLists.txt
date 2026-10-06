project(sovkit_log LANGUAGES CXX)

set(SOVKIT_LOG_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/liblog/liblog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/liblog/runtime_logger.h)
set(SOVKIT_LOG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/liblog.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_logger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_file_sink.cc)

find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
get_target_property(
  SOVKIT_LOG_SPDLOG_INCLUDE_DIRS
  spdlog::spdlog_header_only INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(
  SOVKIT_LOG_FMT_INCLUDE_DIRS
  fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)

add_library(sovkit_log STATIC ${SOVKIT_LOG_PUBLIC_HEADERS} ${SOVKIT_LOG_SOURCES})
add_library(Sovkit::log ALIAS sovkit_log)

set_target_properties(
  sovkit_log
  PROPERTIES CXX_STANDARD 20
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF
             POSITION_INDEPENDENT_CODE TRUE
             CXX_VISIBILITY_PRESET hidden
             C_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN ON
             OUTPUT_NAME sovkit_log)

target_include_directories(
  sovkit_log
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../libsys/include" ${SOVKIT_LOG_SPDLOG_INCLUDE_DIRS} ${SOVKIT_LOG_FMT_INCLUDE_DIRS})
target_compile_definitions(
  sovkit_log PRIVATE FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
target_link_libraries(sovkit_log PUBLIC Threads::Threads)
target_link_libraries(sovkit_log PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(
    sovkit_log PRIVATE /W4 /permissive- /external:W0 /utf-8)
else()
  target_compile_options(sovkit_log PRIVATE -Wall -Wextra -Wpedantic)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOVKIT_LOG_PUBLIC_HEADERS}
                                                   ${SOVKIT_LOG_SOURCES})
