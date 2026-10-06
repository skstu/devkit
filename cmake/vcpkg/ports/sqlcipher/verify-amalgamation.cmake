# Expected bytes from the unmodified, SHA512-pinned v4.19.0 source archive.
# Fail closed if a stale host package, different generator, or changed source
# would silently supply a different SQLite implementation to a cross build.
function(sqlcipher_verify_amalgamation directory)
    set(sqlite3_c_sha512 6cae8d61b2f17205945c017f5a76ac4380bbc5315b8e088b2b6ad748653552d58eb9989345b29c3ccebcfcdb15ada50463f51632dc97103af54f6a840747147d)
    set(sqlite3_h_sha512 35dd5edb874308cc87e1056101e7b21c089bfaa77da78af0fd8ed2890759c73fd05c56accc6d97c109d73b8dee766b95136ad4b5ed935923b0815cbeb4d45f04)
    set(sqlite3ext_h_sha512 b6a838e9802c5e694975803454f5ec94410d79d1228722e981379a3d3e78d4ca85984325554ff1d7f497bdaf887c5fedde035479b3b576c4c0d9a8f38fa8245d)
    foreach(filename IN ITEMS sqlite3.c sqlite3.h sqlite3ext.h)
        string(REPLACE "." "_" key "${filename}")
        file(SHA512 "${directory}/${filename}" actual)
        if(NOT actual STREQUAL "${${key}_sha512}")
            message(FATAL_ERROR "SQLCipher ${filename} SHA512 mismatch: ${actual}")
        endif()
    endforeach()
endfunction()
