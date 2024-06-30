cmake_minimum_required(VERSION 3.12)
project (Win32)
set(CMAKE_CXX_STANDARD 20)

set(SOLUTION_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(LOCAL_CMAKE_MODULES_PATH ${SOLUTION_ROOT_DIR}/cmake/modules/)
set(SOLUTION_INCLUDE_DIR ${SOLUTION_ROOT_DIR}/include/)

LIST(APPEND CMAKE_MODULE_PATH ${LOCAL_CMAKE_MODULES_PATH})
include(global)

if(MSVC)
include(msvc)
option(ENABLE_MSVC_CRT_MT "Enable msvc crt /MT /MTd compile." ON)
endif(MSVC)

add_definitions(-DUNICODE -D_UNICODE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;" CACHE STRING "info" FORCE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

# 3rdparty
add_subdirectory(${SOLUTION_ROOT_DIR}/3rdparty/dlfcn-win32/ "dlfcn-win32")
set_target_properties(dl PROPERTIES FOLDER "3rdparty")

add_subdirectory(${SOLUTION_ROOT_DIR}/3rdparty/libuiohook/ "libuiohook")
set_target_properties(uiohook PROPERTIES FOLDER "3rdparty")


set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
add_subdirectory(${SOLUTION_ROOT_DIR}/3rdparty/fmt/ "fmt")
set_target_properties(fmt PROPERTIES FOLDER "3rdparty")

if(MSVC AND ENABLE_MSVC_CRT_MT)
MSVC_CRT_SET(dl "/MT")
MSVC_CRT_SET(uiohook "/MT")
MSVC_CRT_SET(fmt "/MT")
endif(MSVC AND ENABLE_MSVC_CRT_MT)

#[[
OutputPathConfig("bin" "libs/runtimes/" "libs/runtimes/")
add_subdirectory(${SOLUTION_ROOT_DIR}/runtimes/)
OutputPathConfig("")
option(ENABLE_MAIN_PROJECT_COMPILE_TESTS "Enable main projcet tests compile." ON)
if(ENABLE_MAIN_PROJECT_COMPILE_TESTS)
OutputPathConfig("bin" "bin/tests/" "objs")
add_subdirectory(${SOLUTION_ROOT_DIR}/tests/)
#set_target_properties(tests PROPERTIES FOLDER "tests")
OutputPathConfig("")
endif(ENABLE_MAIN_PROJECT_COMPILE_TESTS)
]]
#[[
]]


add_subdirectory(${SOLUTION_ROOT_DIR}/tests/)
#set_target_properties(tests PROPERTIES FOLDER "tests")

add_subdirectory(${SOLUTION_ROOT_DIR}/projects/)
