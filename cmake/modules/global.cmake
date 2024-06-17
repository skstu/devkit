macro(GLOBAL_ENUM_DIR_SOURCE INPUT_DIR OUTPUT_HEADERS OUTPUT_SOURCES)
file(GLOB_RECURSE ${OUTPUT_HEADERS} "${INPUT_DIR}/*.h" "${INPUT_DIR}/*.hpp")
file(GLOB_RECURSE ${OUTPUT_SOURCES} 
"${INPUT_DIR}/*.c" 
"${INPUT_DIR}/*.cc" 
"${INPUT_DIR}/*.cpp"
"${INPUT_DIR}/*.cxx"
"${INPUT_DIR}/*.rc" 
"${INPUT_DIR}/*.def")
endmacro(GLOBAL_ENUM_DIR_SOURCE)

macro(GLOBAL_GET_TARGET_PROJECT_RELATIVE_PATHNAME INPUT_FULLPATH OUTPUT_PROJECT_PATHNAME)
message(${INPUT_FULLPATH})
get_filename_component(temp_dir_name ${INPUT_FULLPATH} DIRECTORY)
message(${temp_dir_name})
get_filename_component(base_path ${temp_dir_name} DIRECTORY)
get_filename_component(base_path ${base_path} DIRECTORY)
message(${base_path})
file(RELATIVE_PATH ${OUTPUT_PROJECT_PATHNAME} ${base_path} ${temp_dir_name})
endmacro(GLOBAL_GET_TARGET_PROJECT_RELATIVE_PATHNAME)


macro(GLOBAL_SET_AS_STARTUP_PROJECT PROJECTNAME)
target_compile_features(${PROJECTNAME} 
PRIVATE 
${PRJ_COMPILE_FEATURES}
)
endmacro(GLOBAL_SET_AS_STARTUP_PROJECT)

macro(GLOBAL_SET_CMAKE_CONFIGURATION_TYPES ARGS)
if(ARGS STREQUAL "")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;" CACHE STRING "info" FORCE)
else()
set(CMAKE_CONFIGURATION_TYPES ${ARGS} CACHE STRING "info" FORCE)
endif(ARGS STREQUAL "")
endmacro(GLOBAL_SET_CMAKE_CONFIGURATION_TYPES)

macro(GLOBAL_SET_CMAKETARGETS_FOLDER)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
endmacro(GLOBAL_SET_CMAKETARGETS_FOLDER)

#argv#	#是一个下标，0指向第一个参数，累加
#argv	所有的定义时要求传入的参数
#argn	定义时要求传入的参数以外的参数
#argc	传入的实际参数的个数，也就是调用函数是传入的参数个数
#argv0 peOutPath
#argv1 objOutPath
#argv2 libOutPath
#[[
macro(GLOBAL_OUTPATHCONFIG arg)
set(peOutPath "")
set(objOutPath "")
set(libOutPath "")
set(bSpecifiedPath OFF)

if(${ARGC} GREATER 0)
set(peOutPath ${ARGV0})
endif(${ARGC} GREATER 0)
if(${ARGC} GREATER 1)
set(objOutPath ${ARGV1})
endif(${ARGC} GREATER 1)
if(${ARGC} GREATER 2)
set(libOutPath ${ARGV2})
endif(${ARGC} GREATER 2)
if(${ARGC} GREATER 3)
set(bSpecifiedPath ${ARGV3})
endif(${ARGC} GREATER 3)

if(NOT("${peOutPath}" STREQUAL ""))
set(PUBLISH_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/${peOutPath})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PUBLISH_RUNTIME})
if(bSpecifiedPath)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_RUNTIME})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_RUNTIME})
else()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Release)
endif(bSpecifiedPath)
endif(NOT("${peOutPath}" STREQUAL ""))

if(NOT("${objOutPath}" STREQUAL ""))
set(LIBRARY_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/${objOutPath})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${LIBRARY_RUNTIME})
if(bSpecifiedPath)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${LIBRARY_RUNTIME})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${LIBRARY_RUNTIME})
else()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${LIBRARY_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${LIBRARY_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Release)
endif(bSpecifiedPath)
endif(NOT("${objOutPath}" STREQUAL ""))

if(NOT("${libOutPath}" STREQUAL ""))
set(ARCHIVE_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/${libOutPath})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ARCHIVE_RUNTIME})
if(bSpecifiedPath)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${ARCHIVE_RUNTIME})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${ARCHIVE_RUNTIME})
else()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${ARCHIVE_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${ARCHIVE_RUNTIME}/${CMAKE_VS_PLATFORM_NAME}/Release)
endif(bSpecifiedPath)
endif(NOT("${libOutPath}" STREQUAL ""))

endmacro(GLOBAL_OUTPATHCONFIG)
]]

#argv#	#是一个下标，0指向第一个参数，累加
#argv	所有的定义时要求传入的参数
#argn	定义时要求传入的参数以外的参数
#argc	传入的实际参数的个数，也就是调用函数是传入的参数个数
#argv0 LibraryFName
#argv1 RuntimeFName
#argv2 ArchiveFName
macro(GLOBAL_OUTPATHCONFIG arg)
if (CMAKE_CL_64)
set(COMPILE_PLATFORM x64)
else(CMAKE_CL_64)
set(COMPILE_PLATFORM x86)
endif(CMAKE_CL_64)

set(LibraryFName "bin")
set(RuntimeFName "bin")
set(ArchiveFName "librarys")
set(bSpecifiedPath OFF)

if(NOT(ARGV0 STREQUAL ""))
set(LibraryFName ${ARGV0})
elseif(ARGV0 STREQUAL "")
set(LibraryFName "")
endif(NOT(ARGV0 STREQUAL ""))

if(${ARGC} GREATER 1)
set(RuntimeFName ${ARGV1})
endif(${ARGC} GREATER 1)
if(${ARGC} GREATER 2)
set(ArchiveFName ${ARGV2})
endif(${ARGC} GREATER 2)

if(${ARGC} LESS 2)
set(RuntimeFName "")
endif(${ARGC} LESS 2)

if(${ARGC} LESS 3)
set(ArchiveFName "")
endif(${ARGC} LESS 3)

if(${ARGC} GREATER 3)
set(bSpecifiedPath ON)
endif(${ARGC} GREATER 3)

set(PUBLISH_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/${LibraryFName})
set(PUBLISH_RUNTIME ${CMAKE_CURRENT_SOURCE_DIR}/${RuntimeFName})
set(PUBLISH_ARCHIVE ${CMAKE_CURRENT_SOURCE_DIR}/${ArchiveFName})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PUBLISH_LIBRARY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PUBLISH_RUNTIME})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PUBLISH_ARCHIVE})

if(bSpecifiedPath)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_LIBRARY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_LIBRARY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_RUNTIME})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_RUNTIME})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_ARCHIVE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_ARCHIVE})
#message(STATUS "bSpecifiedPath is set ON.")
else(bSpecifiedPath)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_LIBRARY}/${COMPILE_PLATFORM}/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_LIBRARY}/${COMPILE_PLATFORM}/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_RUNTIME}/${COMPILE_PLATFORM}/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_RUNTIME}/${COMPILE_PLATFORM}/Release)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${PUBLISH_ARCHIVE}/${COMPILE_PLATFORM}/Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${PUBLISH_ARCHIVE}/${COMPILE_PLATFORM}/Release)
endif(bSpecifiedPath)

#message(STATUS "CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
#message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
#message(STATUS "CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
#[[
message(STATUS "LibraryFName is ${LibraryFName}")
message(STATUS "RuntimeFName is ${RuntimeFName}")
message(STATUS "ArchiveFName is ${ArchiveFName}")
message(STATUS "bSpecifiedPath is ${bSpecifiedPath}")
message(STATUS "ARGC is ${ARGC}.")
message(STATUS "LibraryFName is ${LibraryFName}")
message(STATUS "RuntimeFName is ${RuntimeFName}")
message(STATUS "ArchiveFName is ${ArchiveFName}")
message(STATUS "bSpecifiedPath is ${bSpecifiedPath}")
]]
endmacro(GLOBAL_OUTPATHCONFIG)





