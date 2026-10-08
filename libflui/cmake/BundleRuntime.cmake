# Consumer API: copy the complete tested runtime without Flutter-specific setup.
function(libflui_bundle_runtime target)
  if(NOT APPLE OR NOT TARGET "${target}")
    message(FATAL_ERROR "libflui_bundle_runtime currently supports macOS targets")
  endif()
  get_target_property(is_bundle "${target}" MACOSX_BUNDLE)
  if(is_bundle)
    set(destination "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks")
    set_property(TARGET "${target}" APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    set_property(TARGET "${target}" APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
  else()
    message(FATAL_ERROR "libflui_bundle_runtime expects a MACOSX_BUNDLE application")
  endif()
  set_property(TARGET "${target}" PROPERTY BUILD_WITH_INSTALL_RPATH ON)
  add_custom_command(TARGET "${target}" POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:devkit::libflui>" "${destination}/libflui.1.dylib"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}/libflui_runtime.bundle"
    COMMAND /usr/bin/ditto "${LIBFLUI_RUNTIME_DIR}" "${destination}/libflui_runtime.bundle"
    COMMAND "${CMAKE_COMMAND}" "-DAPP_PLIST=$<TARGET_BUNDLE_DIR:${target}>/Contents/Info.plist" -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ConfigureApp.cmake"
    VERBATIM)
endfunction()
