project(${entry_name})

GFN_PROJECT_SOURCE_GET(${CMAKE_CURRENT_SOURCE_DIR} OUT_HEADER_S OUT_SOURCE_S)
set(SOURCES ${OUT_HEADER_S} ${OUT_SOURCE_S})

if(MSVC)
    add_compile_options(/utf-8)
endif()
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} 
PRIVATE
${SOLUTION_INCLUDE_DIR}
${SOLUTION_ROOT_DIR}/3rdparty/dlfcn-win32/src/
${SOLUTION_ROOT_DIR}/3rdparty/fmt/include/
${CMAKE_CURRENT_SOURCE_DIR}/../include/
)

target_link_libraries(${PROJECT_NAME}
gdiplus
Shcore
Shlwapi
${SOLUTION_ROOT_DIR}/libs/${CMAKE_VS_PLATFORM_NAME}/${VS_CONFIGURATION_TYPE}/dl.lib
${SOLUTION_ROOT_DIR}/libs/${CMAKE_VS_PLATFORM_NAME}/${VS_CONFIGURATION_TYPE}/stl.lib
${SOLUTION_ROOT_DIR}/libs/${CMAKE_VS_PLATFORM_NAME}/${VS_CONFIGURATION_TYPE}/fmt.lib
)

if(MSVC)
MSVC_LINK_FLAGS_SET_WINDOWS(${PROJECT_NAME})
MSVC_CRT_SET(${PROJECT_NAME} "/MT")
MSVC_USE_PCH(${PROJECT_NAME} ${entry}/stdafx.h ${entry}/stdafx.cc)
endif()