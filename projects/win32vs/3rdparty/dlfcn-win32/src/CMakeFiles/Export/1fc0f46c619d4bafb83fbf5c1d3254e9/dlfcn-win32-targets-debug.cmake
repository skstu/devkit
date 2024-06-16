#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "dlfcn-win32::dl" for configuration "Debug"
set_property(TARGET dlfcn-win32::dl APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(dlfcn-win32::dl PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/lib/dl.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/dl.dll"
  )

list(APPEND _cmake_import_check_targets dlfcn-win32::dl )
list(APPEND _cmake_import_check_files_for_dlfcn-win32::dl "${_IMPORT_PREFIX}/lib/dl.lib" "${_IMPORT_PREFIX}/bin/dl.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
