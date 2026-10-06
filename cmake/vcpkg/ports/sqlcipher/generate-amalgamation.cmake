cmake_minimum_required(VERSION 3.21)

# Mirror upstream main.mk's .target_source/sqlite3.c rules using CMake process
# arguments and file operations, so neither a POSIX shell nor nmake is needed.
function(run)
    execute_process(COMMAND ${ARGV}
        WORKING_DIRECTORY "${OUTPUT_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
endfunction()
function(capture filename)
    execute_process(COMMAND ${ARGN}
        WORKING_DIRECTORY "${OUTPUT_DIR}"
        OUTPUT_FILE "${OUTPUT_DIR}/${filename}"
        COMMAND_ERROR_IS_FATAL ANY)
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIR}/tsrc")
file(COPY "${SOURCE_DIR}/tool/lempar.c"
          "${SOURCE_DIR}/src/parse.y"
          "${SOURCE_DIR}/ext/fts5/fts5parse.y"
    DESTINATION "${OUTPUT_DIR}")
run("${LEMON}" -S parse.y)
run("${LEMON}" -S fts5parse.y)
capture(keywordhash.h "${MKKEYWORDHASH}")
run("${TCLSH}" "${SOURCE_DIR}/tool/mksqlite3h.tcl" "${SOURCE_DIR}" -o sqlite3.h)
run("${TCLSH}" "${SOURCE_DIR}/tool/mkctimec.tcl" ctime.c)
run("${TCLSH}" "${SOURCE_DIR}/tool/mkpragmatab.tcl" pragma.h)

file(READ "${OUTPUT_DIR}/parse.h" parse_header)
file(READ "${SOURCE_DIR}/src/vdbe.c" vdbe_source)
file(WRITE "${OUTPUT_DIR}/opcode-input" "${parse_header}${vdbe_source}")
execute_process(COMMAND "${TCLSH}" "${SOURCE_DIR}/tool/mkopcodeh.tcl"
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    INPUT_FILE "${OUTPUT_DIR}/opcode-input"
    OUTPUT_FILE "${OUTPUT_DIR}/opcodes.h"
    COMMAND_ERROR_IS_FATAL ANY)
capture(opcodes.c "${TCLSH}" "${SOURCE_DIR}/tool/mkopcodec.tcl" opcodes.h)
run("${TCLSH}" "${SOURCE_DIR}/ext/fts5/tool/mkfts5c.tcl")

# These directories contain the components enumerated by mksqlite3c.tcl.
# Only that upstream script selects which files enter the amalgamation.
foreach(directory IN ITEMS src ext/fts3 ext/icu ext/rtree ext/session ext/rbu)
    file(GLOB components "${SOURCE_DIR}/${directory}/*.c" "${SOURCE_DIR}/${directory}/*.h")
    file(COPY ${components} DESTINATION "${OUTPUT_DIR}/tsrc")
endforeach()
file(COPY "${SOURCE_DIR}/ext/misc/stmt.c"
          "${SOURCE_DIR}/ext/fts5/fts5.h"
    DESTINATION "${OUTPUT_DIR}/tsrc")
foreach(filename IN ITEMS keywordhash.h sqlite3.h ctime.c pragma.h opcodes.h
                          opcodes.c parse.c parse.h fts5.c)
    file(COPY "${OUTPUT_DIR}/${filename}" DESTINATION "${OUTPUT_DIR}/tsrc")
endforeach()
execute_process(COMMAND "${TCLSH}" "${SOURCE_DIR}/tool/vdbe-compress.tcl"
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    INPUT_FILE "${SOURCE_DIR}/src/vdbe.c"
    OUTPUT_FILE "${OUTPUT_DIR}/tsrc/vdbe.c"
    COMMAND_ERROR_IS_FATAL ANY)
run("${TCLSH}" "${SOURCE_DIR}/tool/mksqlite3c.tcl" --linemacros=0)
file(COPY "${SOURCE_DIR}/src/sqlite3ext.h" DESTINATION "${OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/verify-amalgamation.cmake")
sqlcipher_verify_amalgamation("${OUTPUT_DIR}")
