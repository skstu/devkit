project(${entry_name})

GFN_PROJECT_SOURCE_GET(${CMAKE_CURRENT_SOURCE_DIR} OUT_HEADER_S OUT_SOURCE_S)
set(SOURCES ${OUT_HEADER_S} ${OUT_SOURCE_S})

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME}
PRIVATE 
${CMAKE_CURRENT_SOURCE_DIR}/../../include/
${SOLUTION_INCLUDE_DIR}
${SOLUTION_ROOT_DIR}/third_party/
)
  
target_link_directories(${PROJECT_NAME}
PRIVATE
${SOLUTION_ROOT_DIR}/libs/runtimes/${CMAKE_VS_PLATFORM_NAME}/
${SOLUTION_ROOT_DIR}/libs/third_party/${CMAKE_VS_PLATFORM_NAME}/
)

if(MSVC)
MSVC_LINK_FLAGS_SET_CONSOLE(${PROJECT_NAME})
MSVC_CRT_SET(${PROJECT_NAME} "/MT")
MSVC_USE_PCH(${PROJECT_NAME} ${entry}/stdafx.h ${entry}/stdafx.cpp)
endif()