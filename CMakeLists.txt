cmake_minimum_required(VERSION 2.8)
project(autoWhiteBalance LANGUAGES C)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
SET(CMAKE_BUILD_TYPE "Release")
SET(PROJECT_SOURCES main.c)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if(LINUX)
    SET(LIBRARIES m)
else()
    SET(LIBRARIES comdlg32)
endif()

include_directories(include)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})
