project (${entry_name})

GFN_PROJECT_SOURCE_GET(${CMAKE_CURRENT_SOURCE_DIR} OUT_HEADER_S OUT_SOURCE_S)
set(SOURCES ${OUT_HEADER_S} ${OUT_SOURCE_S})


if(MSVC)
    add_compile_options(/wd4996 /wd4267)
endif()


#add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(${PROJECT_NAME} SHARED ${SOURCES})
add_definitions(-DSHARED_IMPLEMENTATION)

target_include_directories(${PROJECT_NAME} 
PRIVATE
${SOLUTION_INCLUDE_DIR}
${SOLUTION_ROOT_DIR}/3rdparty/dlfcn-win32/src/
${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
${SOLUTION_ROOT_DIR}/libs/${CMAKE_VS_PLATFORM_NAME}/${VS_CONFIGURATION_TYPE}/dl.lib
${SOLUTION_ROOT_DIR}/libs/${CMAKE_VS_PLATFORM_NAME}/${VS_CONFIGURATION_TYPE}/stl.lib
)

if(MSVC)
#MSVC_LINK_FLAGS_SET_CONSOLE(${PROJECT_NAME})
MSVC_CRT_SET(${PROJECT_NAME} "/MT")
MSVC_USE_PCH(${PROJECT_NAME} ${entry}/src/stdafx.h ${entry}/src/stdafx.cpp)
endif()
