project(libengjs)

gfn_project_source_get(${CMAKE_CURRENT_SOURCE_DIR} OUT_HEADER_S OUT_SOURCE_S)
set(SOURCES ${OUT_HEADER_S} ${OUT_SOURCE_S})

add_library(${PROJECT_NAME} STATIC ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  POSITION_INDEPENDENT_CODE TRUE)
if(MSVC)
  target_compile_options(
    ${PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:MSVC>:/W3>
                            $<$<C_COMPILER_ID:MSVC>:/wd4244>
                            $<$<C_COMPILER_ID:MSVC>:/wd4996>)
endif(MSVC)
# PUBLIC: consumers that link libengjs automatically get these include dirs.
# PRIVATE: build-only headers not needed by consumers.
target_include_directories(
  ${PROJECT_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
         ${xs3RDPARTY_DIR}
         ${xsINCLUDE_DIR}
  PRIVATE ${xs3RDPARTY_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          ${xs3RDPARTY_DIR}/dispatch
          ${xs3RDPARTY_DIR}/cpp-httplib
          ${xs3RDPARTY_DIR}/utfcpp/source
          ${xsVCPKG_INCLUDE_DIR}
          ${xs3RDPARTY_DIR}/websocketpp
          ${xs3RDPARTY_DIR}/asio/include
          ${xsQUICKJS_NG_INCLUDE})
target_link_directories(
  ${PROJECT_NAME} PRIVATE ${xsLIBS_DIR} ${xsLIBS_LIB_DIR}
  ${xsBUILD_INSTALLED_LIB_DIR} ${xsVCPKG_DIR})
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC libstl ${xsFMT_LIBRARY}
                                             ${xsQUICKJS_NG_LIBRARY}
                                      PRIVATE nlohmann_json::nlohmann_json)


# xs_set_target_output_dir(${PROJECT_NAME})
set(INSTALL_BIN_DIR
    "${xsBASE_INSTALL_PREFIX}/bin"
    CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR
    "${xsBASE_INSTALL_PREFIX}/lib"
    CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR
    "${xsBASE_INSTALL_PREFIX}/include"
    CACHE PATH "Installation directory for headers")
set(INSTALL_MAN_DIR
    "${xsBASE_INSTALL_PREFIX}/share/man"
    CACHE PATH "Installation directory for manual pages")
set(INSTALL_PKGCONFIG_DIR
    "${xsBASE_INSTALL_PREFIX}/share/pkgconfig"
    CACHE PATH "Installation directory for pkgconfig (.pc) files")

set(PROJECT_PC ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.pc.cmakein
               ${PROJECT_PC} @ONLY)
# configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.cmakein
# ${CMAKE_CURRENT_BINARY_DIR}/zconf.h @ONLY)

# Install into config-specific subdirs (Debug/Release) when appropriate. For
# multi-config generators use the generator-expression $<CONFIG> so the install
# step picks the correct configuration at install time.
if(CMAKE_CONFIGURATION_TYPES)
  set(INSTALL_CONFIG_DIR "$<CONFIG>")
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        "Debug"
        CACHE STRING "Build type" FORCE)
  endif()
  set(INSTALL_CONFIG_DIR "${CMAKE_BUILD_TYPE}")
endif()

install(
  TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION "${INSTALL_BIN_DIR}/${INSTALL_CONFIG_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}/${INSTALL_CONFIG_DIR}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}/${INSTALL_CONFIG_DIR}")
install(FILES ${OUT_HEADER_S} DESTINATION "${INSTALL_INC_DIR}")
# install(FILES zlib.3 DESTINATION "${INSTALL_MAN_DIR}/man3")
install(FILES ${PROJECT_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")
