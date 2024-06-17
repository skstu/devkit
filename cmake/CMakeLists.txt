cmake_minimum_required(VERSION 3.28.0)
project (devkit)
set(CMAKE_CXX_STANDARD 17)
# 添加预定义宏
add_definitions(-DUNICODE -D_UNICODE)

# 设置默认编译类型
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;" CACHE STRING "info" FORCE)

set(SOLUTION_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../)
set(LOCAL_CMAKE_MODULES_PATH ${SOLUTION_ROOT_DIR}/cmake/modules/)
set(SOLUTION_INCLUDE_DIR ${SOLUTION_ROOT_DIR}/include/)

if(MSVC)
option(ENABLE_MSVC_CRT_MT "Enable msvc crt /MT /MTd compile." ON)
if(ENABLE_MSVC_CRT_MT)
add_compile_options("$<$<CONFIG:Release>:/GL>")
add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif(ENABLE_MSVC_CRT_MT)
endif(MSVC)

LIST(APPEND CMAKE_MODULE_PATH ${LOCAL_CMAKE_MODULES_PATH})
include(msvc_methods)
include(global_methods)
include(OutputPathConfig)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

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


#OutputPathConfig("bin" "bin/projects/" "objs")
#add_subdirectory(${SOLUTION_ROOT_DIR}/projects/)
#set_target_properties(tests PROPERTIES FOLDER "tests")
#OutputPathConfig("")