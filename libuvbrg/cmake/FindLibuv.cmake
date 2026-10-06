find_path(Libuv_INCLUDE_DIR NAMES uv.h)
find_library(Libuv_LIBRARY NAMES uv libuv)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Libuv
  REQUIRED_VARS Libuv_LIBRARY Libuv_INCLUDE_DIR)

if(Libuv_FOUND AND NOT TARGET Libuv::Libuv)
  add_library(Libuv::Libuv UNKNOWN IMPORTED)
  set_target_properties(
    Libuv::Libuv PROPERTIES
    IMPORTED_LOCATION "${Libuv_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${Libuv_INCLUDE_DIR}")
endif()

mark_as_advanced(Libuv_INCLUDE_DIR Libuv_LIBRARY)