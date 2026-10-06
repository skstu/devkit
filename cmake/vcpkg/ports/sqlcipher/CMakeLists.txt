cmake_minimum_required(VERSION 3.21)
project(sqlcipher VERSION 4.19.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

set(SQLCIPHER_SOURCE_DIR "" CACHE PATH "Pinned SQLCipher release source tree")
set(SQLCIPHER_AMALGAMATION_DIR "" CACHE PATH "Amalgamation produced by the native host package")
file(STRINGS "${SQLCIPHER_SOURCE_DIR}/VERSION" sqlite_version LIMIT_COUNT 1)
if(NOT sqlite_version STREQUAL "3.53.4")
    message(FATAL_ERROR "SQLCipher 4.19.0 must use its bundled SQLite 3.53.4")
endif()

if(NOT SQLCIPHER_AMALGAMATION_DIR)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Cross builds require SQLCIPHER_AMALGAMATION_DIR from the native host package")
    endif()
    set(SQLCIPHER_AMALGAMATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/amalgamation")
    file(MAKE_DIRECTORY "${SQLCIPHER_AMALGAMATION_DIR}")

    # Upstream main.mk explicitly supports its bundled Jim Tcl for every code
    # generator. It is pinned by the release hash; no system Tcl is required.
    add_executable(jimsh "${SQLCIPHER_SOURCE_DIR}/autosetup/jimsh0.c")
    if(WIN32)
        target_compile_definitions(jimsh PRIVATE HAVE__FULLPATH)
    else()
        target_compile_definitions(jimsh PRIVATE HAVE_REALPATH)
    endif()
    foreach(tool IN ITEMS lemon mkkeywordhash mksourceid src-verify)
        add_executable(${tool} "${SQLCIPHER_SOURCE_DIR}/tool/${tool}.c")
    endforeach()
    foreach(tool IN ITEMS jimsh lemon mkkeywordhash mksourceid src-verify)
        # The upstream scripts find mksourceid/src-verify in their working dir.
        # The generator expression prevents a Debug/Release subdirectory on MSVC.
        set_target_properties(${tool} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${SQLCIPHER_AMALGAMATION_DIR}/$<0:unused>")
    endforeach()
    file(GLOB_RECURSE generation_inputs
        "${SQLCIPHER_SOURCE_DIR}/src/*"
        "${SQLCIPHER_SOURCE_DIR}/ext/*"
        "${SQLCIPHER_SOURCE_DIR}/tool/*")
    add_custom_command(
        OUTPUT "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.c"
               "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.h"
               "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3ext.h"
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${SQLCIPHER_SOURCE_DIR}"
            "-DOUTPUT_DIR=${SQLCIPHER_AMALGAMATION_DIR}"
            "-DTCLSH=$<TARGET_FILE:jimsh>"
            "-DLEMON=$<TARGET_FILE:lemon>"
            "-DMKKEYWORDHASH=$<TARGET_FILE:mkkeywordhash>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/generate-amalgamation.cmake"
        DEPENDS jimsh lemon mkkeywordhash mksourceid src-verify
            ${generation_inputs}
            "${SQLCIPHER_SOURCE_DIR}/VERSION"
            "${SQLCIPHER_SOURCE_DIR}/manifest"
            "${SQLCIPHER_SOURCE_DIR}/manifest.uuid"
            "${CMAKE_CURRENT_SOURCE_DIR}/generate-amalgamation.cmake"
            "${CMAKE_CURRENT_SOURCE_DIR}/verify-amalgamation.cmake"
        COMMENT "Generating pinned SQLCipher amalgamation with native host tools"
        VERBATIM)
else()
    foreach(filename IN ITEMS sqlite3.c sqlite3.h sqlite3ext.h)
        if(NOT EXISTS "${SQLCIPHER_AMALGAMATION_DIR}/${filename}")
            message(FATAL_ERROR "Missing host-generated ${filename} in ${SQLCIPHER_AMALGAMATION_DIR}")
        endif()
    endforeach()
    include("${CMAKE_CURRENT_SOURCE_DIR}/verify-amalgamation.cmake")
    sqlcipher_verify_amalgamation("${SQLCIPHER_AMALGAMATION_DIR}")
endif()

add_library(sqlcipher STATIC "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.c")
add_library(sqlcipher::sqlcipher ALIAS sqlcipher)
set_target_properties(sqlcipher PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
target_compile_definitions(sqlcipher PRIVATE
    SQLITE_HAS_CODEC
    SQLITE_TEMP_STORE=2
    SQLITE_EXTRA_INIT=sqlcipher_extra_init
    SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLCIPHER_CRYPTO_OPENSSL
    $<$<CONFIG:Debug>:SQLITE_DEBUG>)
target_include_directories(sqlcipher PUBLIC
    "$<BUILD_INTERFACE:${SQLCIPHER_AMALGAMATION_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sqlcipher>")
target_link_libraries(sqlcipher PRIVATE OpenSSL::Crypto Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(sqlcipher PRIVATE m)
endif()
if(ANDROID)
    # SQLCipher's built-in diagnostics/profile callback uses Android logging.
    target_link_libraries(sqlcipher PRIVATE log)
endif()

install(TARGETS sqlcipher EXPORT sqlcipher-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.h"
              "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3ext.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sqlcipher")
# Cross-target packages consume these bytes from the host installation.
install(FILES "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.c"
              "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3.h"
              "${SQLCIPHER_AMALGAMATION_DIR}/sqlite3ext.h"
    DESTINATION share/sqlcipher/amalgamation)
install(EXPORT sqlcipher-targets NAMESPACE sqlcipher::
    DESTINATION share/sqlcipher)
configure_package_config_file(sqlcipher-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/sqlcipher-config.cmake"
    INSTALL_DESTINATION share/sqlcipher)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sqlcipher-config-version.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sqlcipher-config.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/sqlcipher-config-version.cmake"
    DESTINATION share/sqlcipher)
