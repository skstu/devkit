# SQLCipher is always embedded as a static dependency, including when the host
# triplet normally defaults to shared libraries (e.g. x64-windows).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sqlcipher/sqlcipher
    REF "v${VERSION}"
    SHA512 17083306b4e5f4236810b318cbca4c101fa02184f57d90261fd9b2e71c6dbce7d6e6f7586f3f7410a38c49453967a0068938200b434ca3972e19648f810761ef
)

set(amalgamation_options)
if(VCPKG_CROSSCOMPILING)
    # The self host-dependency is resolved by vcpkg before this target port.
    # Only source files cross the host/target boundary; never run target tools.
    list(APPEND amalgamation_options
        "-DSQLCIPHER_AMALGAMATION_DIR=${CURRENT_HOST_INSTALLED_DIR}/share/sqlcipher/amalgamation")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}"
    OPTIONS
        "-DSQLCIPHER_SOURCE_DIR=${SOURCE_PATH}"
        ${amalgamation_options}
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/sqlcipher)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include"
                    "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
